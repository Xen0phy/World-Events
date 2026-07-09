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
#include "nlohmann_json.hpp"
#include <windows.h>
#include <winhttp.h>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <thread>
#include <ctime>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static std::mutex                      s_mutex;
static std::unordered_set<std::string> s_completedWorldBosses; // guarded by s_mutex
static std::unordered_set<std::string> s_claimedMapChests;     // guarded by s_mutex — same cache-day/status as above, one fetch pass covers both

static std::atomic<Gw2ApiStatus> s_status{Gw2ApiStatus::NoKey};
static std::atomic<bool>         s_fetchInProgress{false};

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

// The account/worldbosses list only changes when the player actually
// kills a boss — there's no benefit to polling faster than this, and
// doing so just spends API rate-limit budget (600 req/10min per key) for
// no new information. 2 minutes is frequent enough that a subscription
// disappears from the watchlist shortly after a kill without being
// wasteful.
static constexpr int kMinPollSeconds = 120;

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

    // A stuck/hanging TCP connection (bad wifi, captive portal, etc.)
    // must not leave the fetch thread — and therefore
    // s_fetchInProgress — stuck forever, since that would silently wedge
    // this feature until the addon reloads. 10s is generous for a tiny
    // JSON response but still bounded.
    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

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

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
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

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_completedWorldBosses = std::move(completedWorldBosses);
            s_claimedMapChests     = std::move(claimedMapChests);
        }
        s_cachedForDay.store(today);
        s_status.store(Gw2ApiStatus::Ok);

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
