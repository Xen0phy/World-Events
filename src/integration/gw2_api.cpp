// gw2_api.cpp
// See gw2_api.h for scope/rationale. Implementation notes:
//
// - HTTP via WinHTTP (synchronous calls), always from a short-lived
//   detached background thread — never the render thread. Same overall
//   shape as PasteToChat's background thread in subscriptions.cpp,
//   guarded the same way (an atomic in-flight flag instead of
//   overlapping requests).
// - The completed-boss set is guarded by a mutex; the render thread
//   only ever takes a quick lock to read a handful of strings out of an
//   unordered_set, so there's no meaningful contention risk with a
//   background fetch that runs at most once every kMinPollSeconds.
#include "gw2_api.h"
#include "settings.h"
#include "background_threads.h"
#include "nlohmann_json.hpp"
#include <windows.h>
#include <winhttp.h>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <ctime>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static std::mutex                      s_mutex;
static std::unordered_set<std::string> s_completedWorldBosses; // guarded by s_mutex
static std::unordered_set<std::string> s_claimedMapChests;     // guarded by s_mutex — same cache-day/status as above, one fetch pass covers both

// guarded by s_mutex — key: lowercased objective title, value: complete?
// Same cache-day/status as the two sets above, but a failure on this
// third fetch does NOT invalidate the other two (see PollGw2Api below).
static std::unordered_map<std::string, bool> s_weeklyObjectiveComplete;

static std::atomic<Gw2ApiStatus> s_status{Gw2ApiStatus::NoKey};
static std::atomic<bool>         s_fetchInProgress{false};

// Bumped once, at the very end of a successful poll (see the commit block
// near the end of PollGw2Api below) — see GetGw2ApiFetchGeneration's
// comment in gw2_api.h for why this exists.
static std::atomic<uint64_t> s_fetchGeneration{0};

// UTC day number (days since Unix epoch) the cached set above is valid
// for. -1 = never successfully fetched. Compared against CurrentUtcDay()
// so a daily-reset rollover forces a fresh fetch even if the periodic
// timer (kMinPollSeconds) hasn't elapsed yet.
static std::atomic<long long> s_cachedForDay{-1};

static std::atomic<long long> s_lastFetchAttemptUnixTime{0};

// Real daily reset is UTC midnight, so a plain floor-division day number
// on Unix time (also UTC-based) lines up with it exactly — no timezone
// conversion needed.
static long long CurrentUtcDay()
{
    return (long long)(time(nullptr) / 86400);
}

// ASCII-only lowercase, used solely for matching Wizard's Vault objective
// titles case-insensitively (see s_weeklyObjectiveComplete/
// GetWeeklyObjectiveState below). Every title observed so far is plain
// ASCII English, so no locale/UTF-8 handling is needed here.
static std::string AsciiLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}

// The account/worldbosses list only changes when the player actually
// kills a boss — there's no benefit to polling faster than this, and
// doing so just spends API rate-limit budget (600 req/10min per key) for
// no new information. 2 minutes is frequent enough that a subscription
// disappears from the watchlist shortly after a kill without being
// wasteful.
static constexpr int kMinPollSeconds = 120;

// ---------------------------------------------------------------------------
// In-flight handle tracking / cancellation
// ---------------------------------------------------------------------------
// WinHTTP's documented way to cancel a synchronous call already blocked on
// a handle is to close that handle from a *different* thread — the blocked
// call then returns (with an error) instead of running out its full
// timeout. s_active* below track whichever handles the current HttpsGetJson
// call actually has open; CancelInFlightHttpRequest, registered as a
// shutdown hook (see background_threads.h), closes them from the main/
// render thread during AddonUnload so the fetch thread's blocking call
// returns almost immediately instead of the unload having to wait out the
// full WinHTTP timeout.
//
// s_activeHandlesMutex serializes every close against every other one:
// both HttpsGetJson's own normal end-of-call cleanup and
// CancelInFlightHttpRequest close handles and null out these globals under
// the same lock, so a handle is never closed twice from two threads at
// once — whichever one gets the lock first "wins" and the other sees the
// slot already nulled and skips it.
// ---------------------------------------------------------------------------
static std::mutex s_activeHandlesMutex;
static HINTERNET  s_activeSession = nullptr;
static HINTERNET  s_activeConnect = nullptr;
static HINTERNET  s_activeRequest = nullptr;

static void CancelInFlightHttpRequest()
{
    std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
    // Child handles first, same order normal cleanup uses.
    if (s_activeRequest) { WinHttpCloseHandle(s_activeRequest); s_activeRequest = nullptr; }
    if (s_activeConnect) { WinHttpCloseHandle(s_activeConnect); s_activeConnect = nullptr; }
    if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
}

// Registered once, at static-init time for this translation unit — simpler
// and race-free compared to lazily registering on HttpsGetJson's first
// call (which would need its own synchronization to be correct if two
// fetches ever somehow overlapped).
static bool s_cancelHookRegistered = (RegisterShutdownHook(CancelInFlightHttpRequest), true);

// ---------------------------------------------------------------------------
// HttpsGetJson
// ---------------------------------------------------------------------------
// Synchronous HTTPS GET against `host`+`path`, with an
// "Authorization: Bearer <token>" header. Always called from the
// background fetch thread. Returns false only on a transport-level
// failure (couldn't even get a response); a non-200 HTTP status is
// still reported via outStatusCode with outBody left as whatever the
// server sent (often a small JSON error object), which the caller
// inspects to distinguish "bad key" (401/403) from other errors.
// ---------------------------------------------------------------------------
static bool HttpsGetJson(const wchar_t* host, const wchar_t* path,
                          const std::string& bearerToken,
                          std::string& outBody, int& outStatusCode)
{
    outStatusCode = 0;

    HINTERNET hSession = WinHttpOpen(L"gw2-world-events/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        s_activeSession = hSession;
    }

    // A stuck/hanging TCP connection (bad wifi, captive portal, etc.)
    // must not leave the fetch thread — and therefore
    // s_fetchInProgress — stuck forever, since that would silently wedge
    // this feature until the addon reloads. 10s is generous for a tiny
    // JSON response but still bounded. (CancelInFlightHttpRequest above is
    // the OTHER way this can end early — on an addon unload rather than a
    // hung connection.)
    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        s_activeConnect = hConnect;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        if (s_activeConnect) { WinHttpCloseHandle(s_activeConnect); s_activeConnect = nullptr; }
        if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        s_activeRequest = hRequest;
    }

    std::wstring bearerW(bearerToken.begin(), bearerToken.end()); // API keys are ASCII hex/hyphen, safe to widen byte-for-byte
    std::wstring header = L"Authorization: Bearer " + bearerW;
    WinHttpAddRequestHeaders(hRequest, header.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == TRUE
           && WinHttpReceiveResponse(hRequest, NULL) == TRUE;

    if (ok)
    {
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        outStatusCode = (int)statusCode;

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
        {
            std::string chunk(avail, '\0');
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), avail, &bytesRead) || bytesRead == 0)
                break;
            chunk.resize(bytesRead);
            outBody += chunk;
        }
    }

    // Normal end-of-call cleanup — each close is guarded the same way
    // CancelInFlightHttpRequest's are, so if an unload already force-closed
    // one or more of these (nulling the corresponding s_active* out from
    // under this call, which is exactly why ok/outStatusCode above would
    // have come back false/failed), this doesn't double-close it.
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        if (s_activeRequest) { WinHttpCloseHandle(s_activeRequest); s_activeRequest = nullptr; }
        if (s_activeConnect) { WinHttpCloseHandle(s_activeConnect); s_activeConnect = nullptr; }
        if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// PollGw2Api
// ---------------------------------------------------------------------------
void PollGw2Api()
{
    if (Gw2ApiKey.empty())
    {
        s_status.store(Gw2ApiStatus::NoKey);
        return;
    }

    long long now   = (long long)time(nullptr);
    long long today = CurrentUtcDay();

    bool neverFetched  = s_cachedForDay.load() < 0;
    bool dayRolledOver = !neverFetched && s_cachedForDay.load() != today;
    bool pollDue       = (now - s_lastFetchAttemptUnixTime.load()) >= kMinPollSeconds;

    if (!neverFetched && !dayRolledOver && !pollDue)
        return;

    bool expected = false;
    if (!s_fetchInProgress.compare_exchange_strong(expected, true))
        return; // a fetch is already in flight — this call becomes a no-op

    s_lastFetchAttemptUnixTime.store(now);
    if (s_status.load() == Gw2ApiStatus::NoKey)
        s_status.store(Gw2ApiStatus::Pending);

    std::string apiKeyCopy = Gw2ApiKey; // snapshot: read on the render thread, used on the background thread

    std::thread([apiKeyCopy, today]()
    {
        // Registered for the whole lifetime of this lambda (including every
        // early "return" below) so AddonUnload's WaitForBackgroundThreads
        // can tell this thread is still in flight and wait for it, rather
        // than the DLL potentially being unloaded out from under it mid-
        // request. See background_threads.h.
        BackgroundThreadGuard threadGuard;

        if (IsShuttingDown())
        {
            s_fetchInProgress.store(false);
            return;
        }

        // Small local helper: GET one of the two "already done today" id-list
        // endpoints and parse it into a flat string set. Returns false only
        // on a hard failure (transport error, bad key, malformed body);
        // outStatusCode is always filled in on a completed transport, same
        // convention as HttpsGetJson itself, so the caller can tell a bad
        // key (401/403) apart from a network-layer problem.
        auto FetchIdSet = [&](const wchar_t* path, std::unordered_set<std::string>& outSet, int& outStatusCode) -> bool
        {
            std::string body;
            bool transportOk = HttpsGetJson(L"api.guildwars2.com", path, apiKeyCopy, body, outStatusCode);
            if (!transportOk || outStatusCode != 200) return false;

            try
            {
                json j = json::parse(body);
                if (j.is_array())
                    for (const auto& entry : j)
                        if (entry.is_string())
                            outSet.insert(entry.get<std::string>());
                return true;
            }
            catch (...) { return false; } // malformed body on a 200 — treat like any other failure
        };

        std::unordered_set<std::string> completedWorldBosses;
        int statusCode = 0;
        bool worldBossesOk = FetchIdSet(L"/v2/account/worldbosses", completedWorldBosses, statusCode);

        if (!worldBossesOk)
        {
            // Don't touch either cached set here — a transient failure on
            // this first call shouldn't wipe out otherwise-good data from
            // an earlier successful fetch today; the set just stays
            // exactly as stale/fresh as it already was, and the next poll
            // tries again.
            s_status.store((statusCode == 401 || statusCode == 403) ? Gw2ApiStatus::InvalidKey : Gw2ApiStatus::NetworkError);
            s_fetchInProgress.store(false);
            return;
        }

        // Checkpoint: an unload in progress doesn't need this poll's result
        // any more, and bailing here (rather than after all three calls)
        // shortens how long WaitForBackgroundThreads has to wait — down to
        // whatever's left of the CURRENT call's timeout, instead of up to
        // three sequential calls' worth.
        if (IsShuttingDown())
        {
            s_fetchInProgress.store(false);
            return;
        }

        std::unordered_set<std::string> claimedMapChests;
        bool mapChestsOk = FetchIdSet(L"/v2/account/mapchests", claimedMapChests, statusCode);

        if (!mapChestsOk)
        {
            // Same key just proved good above, so this is a transport/parse
            // hiccup specific to this second call, not a bad key. Neither
            // cached set is updated (see rationale above) — both simply
            // stay at whatever they already were until the next poll,
            // rather than committing worldbosses' fresh data while leaving
            // mapchests silently stale under a "cachedForDay == today"
            // flag that would claim everything is current.
            s_status.store(Gw2ApiStatus::NetworkError);
            s_fetchInProgress.store(false);
            return;
        }

        // Third call: Wizard's Vault weekly objectives. Deliberately a
        // SOFT failure, unlike the two above — this feature is newer, and
        // an older/differently-scoped API key, or ArenaNet changing just
        // this one endpoint, must not take down the two already-working
        // daily checks. On any failure here, weeklyOk stays false and
        // s_weeklyObjectiveComplete below is simply left untouched (same
        // "stale over wrong" rule as everywhere else in this file) —
        // worst case, GetWeeklyObjectiveState just reports NotThisWeek
        // for everything until a later poll succeeds.
        // Same checkpoint as above, one call earlier.
        if (IsShuttingDown())
        {
            s_fetchInProgress.store(false);
            return;
        }

        std::unordered_map<std::string, bool> weeklyComplete;
        bool weeklyOk = false;
        {
            std::string body;
            int wvStatusCode = 0;
            bool transportOk = HttpsGetJson(L"api.guildwars2.com", L"/v2/account/wizardsvault/weekly", apiKeyCopy, body, wvStatusCode);
            if (transportOk && wvStatusCode == 200)
            {
                try
                {
                    json j = json::parse(body);
                    if (j.contains("objectives") && j["objectives"].is_array())
                    {
                        for (const auto& obj : j["objectives"])
                        {
                            if (!obj.contains("title") || !obj["title"].is_string()) continue;
                            std::string title = obj["title"].get<std::string>();
                            int  progressCurrent  = obj.value("progress_current", 0);
                            int  progressComplete = obj.value("progress_complete", 0);
                            bool claimed          = obj.value("claimed", false);
                            bool done = claimed || (progressComplete > 0 && progressCurrent >= progressComplete);
                            weeklyComplete[AsciiLower(title)] = done;
                        }
                    }
                    weeklyOk = true;
                }
                catch (...) { /* malformed body on a 200 — weeklyOk stays false, weeklyComplete discarded below */ }
            }
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_completedWorldBosses = std::move(completedWorldBosses);
            s_claimedMapChests     = std::move(claimedMapChests);
            if (weeklyOk)
                s_weeklyObjectiveComplete = std::move(weeklyComplete);
            // else: leave s_weeklyObjectiveComplete exactly as it was.
        }
        s_cachedForDay.store(today);
        s_status.store(Gw2ApiStatus::Ok);
        // Signal "fresh data landed" to anyone caching derived state off of
        // it (e.g. subscriptions_cache.cpp) — relaxed is fine, this
        // is only ever compared for equality against a previously-read
        // value, never used to order/synchronize access to anything else.
        s_fetchGeneration.fetch_add(1, std::memory_order_relaxed);

        s_fetchInProgress.store(false);
    })
    .detach();
}

Gw2ApiStatus GetGw2ApiStatus()
{
    return s_status.load();
}

bool IsWorldBossCompletedToday(const std::string& worldBossApiId)
{
    if (worldBossApiId.empty()) return false;
    if (s_cachedForDay.load() != CurrentUtcDay()) return false; // no data yet, or stale from a prior UTC day — degrade to "not hidden"

    std::lock_guard<std::mutex> lock(s_mutex);
    return s_completedWorldBosses.count(worldBossApiId) != 0;
}

bool IsMapChestClaimedToday(const std::string& mapChestApiId)
{
    if (mapChestApiId.empty()) return false;
    if (s_cachedForDay.load() != CurrentUtcDay()) return false; // same "no data yet / stale" degradation as IsWorldBossCompletedToday

    std::lock_guard<std::mutex> lock(s_mutex);
    return s_claimedMapChests.count(mapChestApiId) != 0;
}

WeeklyObjectiveState GetWeeklyObjectiveState(const std::string& title)
{
    if (title.empty()) return WeeklyObjectiveState::NotThisWeek;
    if (s_cachedForDay.load() != CurrentUtcDay()) return WeeklyObjectiveState::NotThisWeek; // same "no data yet / stale" degradation as the two daily checks above

    std::string key = AsciiLower(title);
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_weeklyObjectiveComplete.find(key);
    if (it == s_weeklyObjectiveComplete.end()) return WeeklyObjectiveState::NotThisWeek; // not in the live rotation this week (or the soft-fail third fetch hasn't succeeded yet) — either way, not a match
    return it->second ? WeeklyObjectiveState::Complete : WeeklyObjectiveState::Incomplete;
}

std::vector<LiveWeeklyObjective> GetLiveWeeklyObjectives()
{
    if (s_cachedForDay.load() != CurrentUtcDay()) return {}; // same "no data yet / stale" degradation as everywhere else in this file

    std::lock_guard<std::mutex> lock(s_mutex);
    std::vector<LiveWeeklyObjective> out;
    out.reserve(s_weeklyObjectiveComplete.size());
    for (const auto& [titleLower, complete] : s_weeklyObjectiveComplete)
        out.push_back({titleLower, complete});
    return out;
}

uint64_t GetGw2ApiFetchGeneration()
{
    return s_fetchGeneration.load(std::memory_order_relaxed);
}
