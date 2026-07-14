#pragma once
#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// gw2_api.h
// ---------------------------------------------------------------------------
// Thin, narrowly-scoped client for TWO endpoints of the real, public
// GW2 API (api.guildwars2.com):
//   - GET /v2/account/worldbosses — the classic Tyria world bosses the
//     account has already killed since the last daily reset (UTC midnight).
//   - GET /v2/account/mapchests — the Hero's Choice Chests the account has
//     already claimed since the last daily reset. Only the 8 HoT/PoF maps
//     whose CyclicGroup has a non-empty apiMapChestId (see events.h) are
//     ever looked up here; every other id this endpoint returns is ignored.
//
// Not a general GW2 API wrapper: everything else in this addon has NO
// equivalent "already done today" signal anywhere in the public API. A
// WorldEvent with an empty apiWorldBossId, or a CyclicGroup with an empty
// apiMapChestId (see events.h), is simply never affected by this file.
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

// A THIRD endpoint, fetched in the same background pass and on the same
// kMinPollSeconds cadence as worldbosses/mapchests above, but reporting
// something different in kind: rather than an id list this addon can
// directly recognize, each objective comes back with only its own display
// TITLE — Wizard's Vault objective ids aren't documented/stable across
// ArenaNet's seasonal rotation the way worldbosses'/mapchests' ids are, so
// title is the only thing to match against. See weekly_vault.h/.cpp for
// the addon-side table that maps these titles to actual WorldEvent/
// CyclicGroup::Slot entries — this file only exposes the raw API state.
enum class WeeklyObjectiveState
{
    NotThisWeek, // not found in the live objective list at all — covers "genuinely not part of this week's rotation," "no successful fetch yet," and "stale/network/key problem" all the same way, same degrade-safe rule as IsWorldBossCompletedToday/IsMapChestClaimedToday above: never silently treated as complete
    Incomplete,
    Complete,    // progress_complete reached, or already claimed, as of the most recent successful fetch
};

// `title` is matched case-insensitively (ASCII lowercasing only — every
// objective title observed so far is plain ASCII) against each live
// objective's own "title" field. Exact match, not substring — unlike
// weekly_vault.cpp's mapping table, which is free to only cover a subset
// of words, this function compares against the real API string directly
// and has no fuzzy-matching logic of its own.
WeeklyObjectiveState GetWeeklyObjectiveState(const std::string& title);

// Bumped by exactly 1 each time a poll SUCCESSFULLY commits fresh
// worldbosses/mapchests data (the point in PollGw2Api where s_cachedForDay
// is stamped and s_status is set to Ok — see gw2_api.cpp). The weekly
// objectives call can soft-fail independently without preventing this from
// bumping; a caller that only wants to know "is it worth re-checking
// anything" (e.g. subscriptions_weekly_cache.cpp) can cheaply compare this
// against a value it saved last time instead of re-deriving/re-checking
// state on every single frame regardless of whether a new fetch landed.
uint64_t GetGw2ApiFetchGeneration();
