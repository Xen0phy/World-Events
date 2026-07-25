//################################################################################
// gw2_api.cpp
//--------------------------------------------------------------------------------
// See gw2_api.h for scope/rationale. Implementation notes:
//
// - HTTP via WinHTTP (synchronous calls), always from a short-lived
//   detached background thread - never the render thread. Same overall
//   shape as PasteToChat's background thread in subscriptions.cpp,
//   guarded the same way (an atomic in-flight flag instead of
//   overlapping requests).
// - The completed-boss set is guarded by a mutex; the render thread only
//   ever takes a quick lock to read a handful of strings out of an
//   unordered_set, so there's no meaningful contention risk with a
//   background fetch that runs at most once every kMinPollSeconds.
//--------------------------------------------------------------------------------

#pragma comment(lib, "winhttp.lib")

#include "background_threads.h"
#include "gw2_api.h"
#include "nlohmann_json.hpp"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cctype>
#include <ctime>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

static std::mutex                      s_mutex;
static std::unordered_set<std::string> s_completedWorldBosses; //. guarded by s_mutex
static std::unordered_set<std::string> s_claimedMapChests;     //. guarded by s_mutex

//_ Guarded by s_mutex - key: lowercased objective title, value: complete?
// A failure on this third fetch does NOT invalidate the two sets above.
static std::unordered_map<std::string, bool> s_weeklyObjectiveComplete;

static std::atomic<Gw2ApiStatus> s_status{Gw2ApiStatus::NoKey};
static std::atomic<bool>         s_fetchInProgress{false};

//_ Bumped at the end of a successful poll - see GetGw2ApiFetchGeneration's
// comment in gw2_api.h for why this exists.
static std::atomic<uint64_t> s_fetchGeneration{0};

//_ UTC day number the cached set above is valid for; -1 = never fetched.
// A rollover forces a fresh fetch even if kMinPollSeconds hasn't elapsed.
static std::atomic<long long> s_cachedForDay{-1};

static std::atomic<long long> s_lastFetchAttemptUnixTime{0};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CurrentUtcDay
//--------------------------------------------------------------------------------
// Real daily reset is UTC midnight, so a plain floor-division day number
// on Unix time (also UTC-based) lines up with it exactly - no timezone
// conversion needed.
//--------------------------------------------------------------------------------
static long long CurrentUtcDay()
{
    return (long long)(time(nullptr) / 86400);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AsciiLower
//--------------------------------------------------------------------------------
// ASCII-only lowercase, used solely for matching Wizard's Vault objective
// titles case-insensitively (see s_weeklyObjectiveComplete/
// GetWeeklyObjectiveState below). Every title observed so far is plain
// ASCII English, so no locale/UTF-8 handling is needed here.
//--------------------------------------------------------------------------------
static std::string AsciiLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}

//_ No benefit polling faster than this - the list only changes on a kill,
// and 600 req/10min per key is the rate-limit budget to conserve.
static constexpr int kMinPollSeconds = 120;

//_ Handles the in-flight HttpsGetJson call currently has open - see
// CancelInFlightHttpRequest below for why these need tracking.
static std::mutex s_activeHandlesMutex;
static HINTERNET  s_activeSession = nullptr;
static HINTERNET  s_activeConnect = nullptr;
static HINTERNET  s_activeRequest = nullptr;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CancelInFlightHttpRequest
//--------------------------------------------------------------------------------
// WinHTTP's documented way to cancel a blocked synchronous call is to
// close the handle from a different thread - the blocked call then
// returns (with an error) instead of running out its timeout. Registered
// as a shutdown hook (see background_threads.h), this closes whichever
// s_active* handles HttpsGetJson currently has open, from the main/render
// thread during AddonUnload, so the fetch thread's blocking call returns
// almost immediately instead of waiting out the full timeout.
//
// s_activeHandlesMutex serializes every close so a handle is never closed
// twice from two threads at once - whichever close gets the lock first
// "wins"; the other sees the slot already nulled and skips it.
//--------------------------------------------------------------------------------
static void CancelInFlightHttpRequest()
{
    std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
    //_ Child handles first, same order normal cleanup uses.
    if (s_activeRequest) { WinHttpCloseHandle(s_activeRequest); s_activeRequest = nullptr; }
    if (s_activeConnect) { WinHttpCloseHandle(s_activeConnect); s_activeConnect = nullptr; }
    if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
}

//_ Registered at static-init time - simpler/race-free than lazily
// registering on HttpsGetJson's first call.
static bool s_cancelHookRegistered = (RegisterShutdownHook(CancelInFlightHttpRequest), true);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// HttpsGetJson
//--------------------------------------------------------------------------------
// Synchronous HTTPS GET against `host`+`path`, with an "Authorization:
// Bearer <token>" header. Always called from the background fetch thread.
// Returns false only on a transport-level failure; a non-200 status is
// still reported via outStatusCode with outBody left as whatever the
// server sent, which the caller inspects to distinguish "bad key"
// (401/403) from other errors. Tracks its open handles in s_active*
// under s_activeHandlesMutex so CancelInFlightHttpRequest can force-close
// them from another thread.
//--------------------------------------------------------------------------------
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

    //_ A stuck/hanging connection must not leave this thread (and
    // s_fetchInProgress) stuck forever; 10s is generous but bounded.
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

    //_ API keys are ASCII, safe to widen byte-for-byte.
    std::wstring bearerW(bearerToken.begin(), bearerToken.end());
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

    //_ Same guarded-close pattern as CancelInFlightHttpRequest - never
    // double-closes a handle an unload already force-closed.
    {
        std::lock_guard<std::mutex> lock(s_activeHandlesMutex);
        if (s_activeRequest) { WinHttpCloseHandle(s_activeRequest); s_activeRequest = nullptr; }
        if (s_activeConnect) { WinHttpCloseHandle(s_activeConnect); s_activeConnect = nullptr; }
        if (s_activeSession) { WinHttpCloseHandle(s_activeSession); s_activeSession = nullptr; }
    }
    return ok;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PollGw2Api
//--------------------------------------------------------------------------------
// Fetches worldbosses, then mapchests, then wizardsvault/weekly, in that
// order, from a detached background thread (see gw2_api.h for the public
// contract). Either of the first two failing hard-fails the whole poll
// without touching either cached set (stale-over-wrong); the third is a
// soft failure, since a differently-scoped key or an ArenaNet-side change
// to just that endpoint must not take down the two already-working daily
// checks. IsShuttingDown() is checked between calls so an addon unload
// doesn't have to wait out a call it no longer needs the result of.
//--------------------------------------------------------------------------------
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
        return; //. fetch already in flight, no-op

    s_lastFetchAttemptUnixTime.store(now);
    if (s_status.load() == Gw2ApiStatus::NoKey)
        s_status.store(Gw2ApiStatus::Pending);

    std::string apiKeyCopy = Gw2ApiKey; //. snapshot for the background thread

    std::thread([apiKeyCopy, today]()
    {
        //_ Keeps AddonUnload's WaitForBackgroundThreads aware this thread
        // is still in flight, for the lambda's whole lifetime.
        BackgroundThreadGuard threadGuard;

        if (IsShuttingDown())
        {
            s_fetchInProgress.store(false);
            return;
        }

        //_ GETs one of the two "already done today" id-list endpoints into
        // a flat string set; false only on a hard failure (transport,
        // bad key, or malformed body).
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
            catch (...) { return false; } //. malformed body, treated as failure
        };

        std::unordered_set<std::string> completedWorldBosses;
        int statusCode = 0;
        bool worldBossesOk = FetchIdSet(L"/v2/account/worldbosses", completedWorldBosses, statusCode);

        if (!worldBossesOk)
        {
            //_ Leaves both cached sets untouched - a transient failure
            // here shouldn't wipe out an earlier fetch; next poll retries.
            s_status.store((statusCode == 401 || statusCode == 403) ? Gw2ApiStatus::InvalidKey : Gw2ApiStatus::NetworkError);
            s_fetchInProgress.store(false);
            return;
        }

        //_ Bails early on shutdown so WaitForBackgroundThreads waits at
        // most the current call's timeout, not three calls' worth.
        if (IsShuttingDown())
        {
            s_fetchInProgress.store(false);
            return;
        }

        std::unordered_set<std::string> claimedMapChests;
        bool mapChestsOk = FetchIdSet(L"/v2/account/mapchests", claimedMapChests, statusCode);

        if (!mapChestsOk)
        {
            //_ Key already proved good above, so this is transport/parse
            // specific, not a bad key; neither cached set updated (as above).
            s_status.store(Gw2ApiStatus::NetworkError);
            s_fetchInProgress.store(false);
            return;
        }

        //_ Third call, deliberately soft-fail (unlike the two above): a
        // differently-scoped/older key, or an ArenaNet-side change to
        // just this endpoint, must not take down the two daily checks.
        //_ Same checkpoint as above, one call earlier.
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
                //_ Malformed body on a 200 - weeklyOk stays false,
                // weeklyComplete discarded.
                catch (...) { }
            }
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_completedWorldBosses = std::move(completedWorldBosses);
            s_claimedMapChests     = std::move(claimedMapChests);
            if (weeklyOk)
                s_weeklyObjectiveComplete = std::move(weeklyComplete);
            //. else: left unchanged
        }
        s_cachedForDay.store(today);
        s_status.store(Gw2ApiStatus::Ok);
        //_ Signals "fresh data landed" (see subscriptions_cache.cpp);
        // relaxed is fine - only compared for equality, never used to order.
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
    if (s_cachedForDay.load() != CurrentUtcDay()) return false; //. stale/no fetch (see gw2_api.h)

    std::lock_guard<std::mutex> lock(s_mutex);
    return s_completedWorldBosses.count(worldBossApiId) != 0;
}

bool IsMapChestClaimedToday(const std::string& mapChestApiId)
{
    if (mapChestApiId.empty()) return false;
    if (s_cachedForDay.load() != CurrentUtcDay()) return false; //. same as above

    std::lock_guard<std::mutex> lock(s_mutex);
    return s_claimedMapChests.count(mapChestApiId) != 0;
}

WeeklyObjectiveState GetWeeklyObjectiveState(const std::string& title)
{
    if (title.empty()) return WeeklyObjectiveState::NotThisWeek;
    if (s_cachedForDay.load() != CurrentUtcDay()) return WeeklyObjectiveState::NotThisWeek; //. same as above

    std::string key = AsciiLower(title);
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_weeklyObjectiveComplete.find(key);
    //_ Not in this week's live rotation - or the soft-fail third fetch
    // hasn't succeeded yet; either way, not a match.
    if (it == s_weeklyObjectiveComplete.end()) return WeeklyObjectiveState::NotThisWeek;
    return it->second ? WeeklyObjectiveState::Complete : WeeklyObjectiveState::Incomplete;
}

std::vector<LiveWeeklyObjective> GetLiveWeeklyObjectives()
{
    if (s_cachedForDay.load() != CurrentUtcDay()) return {}; //. same as above

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