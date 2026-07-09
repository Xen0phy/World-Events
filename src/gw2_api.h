#pragma once
#include <string>

// ---------------------------------------------------------------------------
// gw2_api.h
// ---------------------------------------------------------------------------
// Thin, narrowly-scoped client for TWO endpoints of the real, public
// GW2 API (api.guildwars2.com):
//   - GET /v2/account/worldbosses — the classic Tyria world bosses the
//     account has already killed since the last daily reset (UTC midnight).
//   - GET /v2/account/mapchests — the Hero's Choice Chests the account has
//     already claimed since the last daily reset. Of the many maps this
//     covers, only the 8 HoT/PoF maps whose CyclicGroup has a non-empty
//     apiMapChestId (see events.h) are ever looked up here — every other
//     id this endpoint can return is simply never queried.
//
// This is deliberately not a general GW2 API wrapper. Everything else in
// this addon (Basic Events other than the 13 Core Bosses, every Cyclic
// Group other than those 8 maps, invasions, Ley Line Anomaly, fractal
// incursions, and convergences) has NO equivalent "already done today"
// signal anywhere in the public API. A WorldEvent with an empty
// apiWorldBossId, or a CyclicGroup with an empty apiMapChestId (see
// events.h for both), is simply never affected by this file, by design.
//
// Requires a user-supplied API key (Gw2ApiKey in settings_table.h) with
// at least the "progression" permission. No key -> PollGw2Api is a
// permanent no-op and both IsWorldBossCompletedToday/
// IsMapChestClaimedToday always return false — i.e. the feature degrades
// to "off", never to "everything hidden".
// ---------------------------------------------------------------------------

// Call once per frame (e.g. from AddonRender) — cheap: internally
// rate-limited and only actually starts a background HTTP request when
// either the poll interval has elapsed or the UTC day has rolled over
// since the last successful fetch. Never blocks the calling thread; the
// actual HTTP GET runs on a short-lived detached background thread, same
// pattern as PasteToChat in subscriptions.cpp.
void PollGw2Api();

// Surfaced for a one-line status indicator in the options panel next to
// the API key field.
enum class Gw2ApiStatus
{
    NoKey,         // Gw2ApiKey is empty — feature is off
    Pending,       // key is set, first fetch hasn't completed yet
    Ok,            // last fetch succeeded; data below is current for today
    InvalidKey,    // last fetch got HTTP 401/403 — key is missing the
                   // "progression" permission, or is wrong/revoked
    NetworkError,  // last fetch failed for any other reason (offline,
                   // API down, malformed response, etc.)
};
Gw2ApiStatus GetGw2ApiStatus();

// True if `worldBossApiId` (a WorldEvent::apiWorldBossId value, e.g.
// "tequatl_the_sunless") is in the set of bosses this account has killed
// since the last UTC daily reset, according to the most recent
// successful fetch.
//
// Always false if: the id is empty, no key is set, no fetch has
// completed yet, or the cached data is from a previous UTC day (should
// self-correct within kMinPollSeconds of the rollover via PollGw2Api,
// but this guards the gap). Unknown/stale always degrades to "not
// completed" (i.e. "not hidden") rather than "completed" — a broken key
// or a network hiccup should never cause a subscription to silently
// disappear.
bool IsWorldBossCompletedToday(const std::string& worldBossApiId);

// True if `mapChestApiId` (a CyclicGroup::apiMapChestId value, e.g.
// "auric_basin_heros_choice_chest") is in the set of Hero's Choice
// Chests this account has claimed since the last UTC daily reset,
// according to the most recent successful fetch.
//
// Same "always false if unknown/stale/no-key" degradation as
// IsWorldBossCompletedToday above — a broken key or a network hiccup
// should never cause a subscription to silently disappear. Fetched in
// the same background pass as worldbosses (see gw2_api.cpp), so this
// becomes current on the same ~2-minute cadence.
bool IsMapChestClaimedToday(const std::string& mapChestApiId);
