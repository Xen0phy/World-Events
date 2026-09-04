//################################################################################
// gw2_api.h
//--------------------------------------------------------------------------------
// PollGw2Api()                    call once per frame; polls all 4 endpoints
// GetGw2ApiStatus()               NoKey/Pending/Ok/InvalidKey/NetworkError
// IsWorldBossCompletedToday(id)   true if boss killed since last UTC reset
// IsMapChestClaimedToday(id)      true if chest claimed since last UTC reset
// GetWeeklyObjectiveState(title)  Wizard's Vault weekly objective progress
// GetLiveWeeklyObjectives()       snapshot of every live weekly objective
// GetGw2ApiFetchGeneration()      bumped on each successful daily-data fetch
// LiveEventsRegion                NA / EU / Unknown
// GetLiveEventsRegion()           NA/EU derived from the account's home world
// LiveEventsRegionToWireString    "NA"/"EU"/"" for the wire protocol
//--------------------------------------------------------------------------------
// Thin, narrowly-scoped client for four endpoints of the real, public GW2 API
// (api.guildwars2.com): world-boss kills, Hero's Choice Chest claims, this week's
// live Wizard's Vault objectives (each scoped to what's already been done since
// the last reset), and the account's home world, for NA/EU region - see
// gw2_api.cpp for endpoint paths and GetLiveEventsRegion below.
//
// Requires a user-supplied API key (Gw2ApiKey in settings_table.h) with at least
// the "progression" and "account" permissions. No key -> PollGw2Api is a
// permanent no-op; every daily-completion query reports "not done"/"not found",
// and GetLiveEventsRegion reports Unknown - features degrade to "off", never to
// "everything hidden". Mumble Link's Identity JSON no longer carries a usable
// world_id, so this is the only region source left, feeding only the cross-region
// toast relay - subscriptions_edit_window.cpp gates on it for that;
// live_events_ui.cpp's report button needs no key.
//
// Degradation rule for the daily-completion queries: unknown/stale/not-yet-
// fetched data is always "not completed"/"not found", never "completed" - a
// broken key must never make a subscription silently disappear. The account-
// world fetch isn't day-scoped (a home world rarely changes); it just reports
// Unknown until the first successful fetch lands.
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
// worldbosses/mapchests, but reporting by display TITLE instead of a stable id,
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
// (substring/keyword matching) instead of checking one exact, already-known title
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LiveEventsRegion
//--------------------------------------------------------------------------------
// NA/EU are the wire values (live-toast-handoff.md section 2); Unknown covers no
// key, a fetch that hasn't landed yet, an invalid/under-permissioned key, or a
// home world outside both ranges. An enum, not a bare wire string: a typo'd
// literal ("Na", "eu", ...) fails to compile instead of silently comparing false
// everywhere - every comparison against a region goes through this type, and the
// wire string is produced in exactly one place (LiveEventsRegionToWireString).
//--------------------------------------------------------------------------------
enum class LiveEventsRegion
{
    Unknown,
    NA,
    EU,
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetLiveEventsRegion
//--------------------------------------------------------------------------------
// NA/EU home-world id ranges (1xxx/2xxx respectively - see live-toast- handoff.md
// section 1 - the same grouping guesting already uses) applied to the most recent
// successful /v2/account fetch's "world" field. Unknown wherever the degradation
// rule (see file header) applies to that fetch - in particular, always Unknown
// with no key set, so callers can gate on this alone instead of separately
// checking Gw2ApiKey. Cheap: reads an already- cached value, safe to call every
// frame.
//--------------------------------------------------------------------------------
LiveEventsRegion GetLiveEventsRegion();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LiveEventsRegionToWireString
//--------------------------------------------------------------------------------
// "NA" / "EU" (live-toast-handoff.md section 2); empty for Unknown - callers must
// check for that before sending/connecting. The only place a LiveEventsRegion
// becomes a wire-protocol string.
//--------------------------------------------------------------------------------
std::string LiveEventsRegionToWireString(LiveEventsRegion region);