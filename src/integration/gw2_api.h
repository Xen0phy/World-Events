//################################################################################
// gw2_api.h
//--------------------------------------------------------------------------------
// PollGw2Api()                    call once per frame; polls all 3 endpoints
// GetGw2ApiStatus()               NoKey/Pending/Ok/InvalidKey/NetworkError
// IsWorldBossCompletedToday(id)   true if boss killed since last UTC reset
// IsMapChestClaimedToday(id)      true if chest claimed since last UTC reset
// GetWeeklyObjectiveState(title)  Wizard's Vault weekly objective progress
// GetLiveWeeklyObjectives()       snapshot of every live weekly objective
// GetGw2ApiFetchGeneration()      bumped on each successful daily-data fetch
//--------------------------------------------------------------------------------
// Thin, narrowly-scoped client for three endpoints of the real, public GW2 API
// (api.guildwars2.com):
//   - GET /v2/account/worldbosses  - classic Tyria world bosses the
//     account has killed since the last daily reset (UTC midnight).
//   - GET /v2/account/mapchests    - Hero's Choice Chests the account has
//     claimed since the last daily reset. Only the 8 HoT/PoF maps whose
//     CyclicGroup has a non-empty apiMapChestId (see events.h) are ever
//     looked up; every other id this endpoint returns is ignored.
//   - GET /v2/account/wizardsvault/weekly - this week's live Wizard's
//     Vault objectives. Unlike the two above, objective ids aren't stable
//     across ArenaNet's seasonal rotation, so objectives are matched by
//     display TITLE instead (case-insensitive ASCII, exact match - see
//     GetWeeklyObjectiveState below). weekly_vault.cpp maps these titles
//     to actual WorldEvent/CyclicGroup::Slot entries; this file only
//     exposes the raw API state.
//
// Not a general GW2 API wrapper: everything else in this addon has no equivalent
// "already done today" signal in the public API. A WorldEvent with an empty
// apiWorldBossId, or a CyclicGroup with an empty apiMapChestId, is simply never
// affected by this file.
//
// Requires a user-supplied API key (Gw2ApiKey in settings_table.h) with at least
// the "progression" permission. No key -> PollGw2Api is a permanent no-op and
// every query function below always reports "not done"/"not found" - i.e. the
// feature degrades to "off", never to "everything hidden".
//
// Degradation rule used throughout this file: unknown, stale (cached for a
// previous UTC day), or not-yet-fetched data is always treated the same as "not
// completed"/"not found", never as "completed" - a broken key or a network hiccup
// must never make a subscription silently disappear.
//--------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PollGw2Api
//--------------------------------------------------------------------------------
// Call once per frame (e.g. from AddonRender) - cheap: internally rate-limited,
// only actually starts a background HTTP request when the poll interval has
// elapsed or the UTC day has rolled over. Never blocks the calling thread - runs
// on a short-lived detached background thread, same pattern as PasteToChat in
// subscriptions.cpp.
//--------------------------------------------------------------------------------
void PollGw2Api();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Gw2ApiStatus
//--------------------------------------------------------------------------------
// Surfaced for a one-line status indicator in the options panel next to the API
// key field.
//--------------------------------------------------------------------------------
enum class Gw2ApiStatus
{
    NoKey,         //. Gw2ApiKey empty - feature off
    Pending,       //. key set - fetch pending
    Ok,            //. fetch ok - data is current
    InvalidKey,    //. last fetch got HTTP 401/403
    NetworkError,  //. last fetch failed for any other reason
};
Gw2ApiStatus GetGw2ApiStatus();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsWorldBossCompletedToday / IsMapChestClaimedToday
//--------------------------------------------------------------------------------
// True if `worldBossApiId`/`mapChestApiId` (a WorldEvent::apiWorldBossId or
// CyclicGroup::apiMapChestId value, e.g. "tequatl_the_sunless" or
// "auric_basin_heros_choice_chest") is in the set of bosses killed / chests
// claimed since the last UTC daily reset, per the most recent successful fetch.
// Both are fetched in the same background pass (see gw2_api.cpp), so both become
// current on the same ~2-minute cadence. False for an empty id, or wherever the
// degradation rule above applies.
//--------------------------------------------------------------------------------
bool IsWorldBossCompletedToday(const std::string& worldBossApiId);
bool IsMapChestClaimedToday(const std::string& mapChestApiId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WeeklyObjectiveState
//--------------------------------------------------------------------------------
// The third endpoint (see file header): fetched on the same cadence as
// worldbosses/mapchests, but reporting by display TITLE rather than a stable id,
// since Wizard's Vault objective ids aren't stable across ArenaNet's seasonal
// rotation. See weekly_vault.h/.cpp for the addon-side table mapping titles to
// actual WorldEvent/ CyclicGroup::Slot entries - this file only exposes the raw
// API state.
//--------------------------------------------------------------------------------
enum class WeeklyObjectiveState
{
    NotThisWeek, //. not in the live objective list
    Incomplete,
    Complete,    //. progress_complete reached, or already claimed
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetWeeklyObjectiveState
//--------------------------------------------------------------------------------
// `title` is matched case-insensitively (ASCII lowercasing only - every observed
// title is plain ASCII) against each live objective's own "title" field. Exact
// match, not substring - unlike weekly_vault.cpp's Cyclic mapping table, which
// only needs a couple of keywords out of the title.
//--------------------------------------------------------------------------------
WeeklyObjectiveState GetWeeklyObjectiveState(const std::string& title);

//********************************************************************************
// LiveWeeklyObjective
//--------------------------------------------------------------------------------
// titleLower   ASCII-lowercased title (see gw2_api.cpp's AsciiLower) -
//              lowercase your own search terms to match against it
// complete     progress_complete reached, or already claimed
//--------------------------------------------------------------------------------
// One live Wizard's Vault objective, with no title matching applied yet - for
// callers that need to search across every live objective at once
// (substring/keyword matching) rather than check one exact, already-known title
// (see GetWeeklyObjectiveState above for that case).
//--------------------------------------------------------------------------------
struct LiveWeeklyObjective
{
    std::string titleLower;
    bool        complete;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetLiveWeeklyObjectives
//--------------------------------------------------------------------------------
// Snapshot of every currently-live weekly objective, or empty wherever the
// degradation rule (see file header) applies. See weekly_vault.cpp's
// IsBasicEventWeeklyTarget/IsCyclicSlotWeeklyTarget for the two matching
// strategies built on top of this.
//--------------------------------------------------------------------------------
std::vector<LiveWeeklyObjective> GetLiveWeeklyObjectives();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetGw2ApiFetchGeneration
//--------------------------------------------------------------------------------
// Bumped by exactly 1 each time a poll successfully commits fresh
// worldbosses/mapchests data (see gw2_api.cpp). The weekly-objectives call can
// soft-fail independently without preventing this from bumping. A caller that
// only wants to know "is it worth re-checking anything" (e.g.
// subscriptions_cache.cpp) can cheaply compare this against a value it saved last
// time, instead of re-deriving state every frame.
//--------------------------------------------------------------------------------
uint64_t GetGw2ApiFetchGeneration();