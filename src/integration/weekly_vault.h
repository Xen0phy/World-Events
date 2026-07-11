#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// weekly_vault.h
// ---------------------------------------------------------------------------
// Wizard's Vault WEEKLY objectives, cross-referenced against this addon's
// Basic Events / Cyclic Group slots.
//
// Completely separate system from, and independent of, the DAILY
// worldbosses/mapchests tracking in gw2_api.h / WorldEvent::apiWorldBossId /
// CyclicGroup::apiMapChestId (events.h). The two can overlap on the same
// physical boss/meta (e.g. killing Tequatl satisfies both the daily
// worldbosses list AND, in a week the Vault rotation includes it, this
// week's weekly objective) but are tracked on entirely separate reset
// schedules and separate live API calls (see gw2_api.h's
// GetWeeklyObjectiveState, GET /v2/account/wizardsvault/weekly).
//
// THE ONE TABLE TO EDIT each time ArenaNet rotates the Vault's objective
// list, or whenever a mapping turns out wrong: g_WeeklyObjectives in
// weekly_vault.cpp. Read the comment at the top of that file first.
// ---------------------------------------------------------------------------

// One event/slot that can satisfy a weekly objective.
struct WeeklyTarget
{
    std::string groupOrEventName; // WorldEvent::name (Basic) or CyclicGroup::name (Cyclic)
    std::string slotName;         // empty = this is a Basic Event; non-empty = this specific CyclicGroup::Slot::name within that group
};

// One weekly Wizard's Vault objective and every target that can satisfy it.
//
// `targets` is an OR list, not an AND list — several real objectives span
// multiple, unrelated maps/chains at once (e.g. "...Cantha or...Maguuma
// Jungle"), and completing ANY ONE of the listed targets is what actually
// completes the objective in-game. The live API only reports the
// objective's own aggregate progress, not which specific target was
// responsible — so once GetWeeklyObjectiveState (gw2_api.h) reports this
// objective Complete, EVERY target listed here stops being treated as an
// active weekly target, regardless of which one the player actually did.
struct WeeklyObjectiveMapping
{
    std::string title; // passed straight through to GetWeeklyObjectiveState (gw2_api.h) — see that function's own comment for its matching rules
    std::vector<WeeklyTarget> targets;
};

// THE editable table. See weekly_vault.cpp.
extern std::vector<WeeklyObjectiveMapping> g_WeeklyObjectives;

// True if `eventName` (a WorldEvent::name) is a target of some weekly
// objective that is genuinely part of THIS WEEK's live Vault rotation —
// i.e. worth auto-tracking / showing a "weekly" marker for at all.
// `outComplete` is only meaningful when this returns true: it reports
// whether that objective has already been completed this week. Callers
// (subscriptions_bar.cpp / subscriptions_window.cpp) are responsible for
// deciding what to do with a completed-but-still-manually-subscribed
// event — this function only reports the raw weekly state.
bool IsBasicEventWeeklyTarget(const std::string& eventName, bool& outComplete);

// Same as above, for one specific Cyclic Group slot (identified by group
// name + the individual CyclicGroup::Slot::name within it).
bool IsCyclicSlotWeeklyTarget(const std::string& groupName, const std::string& slotName, bool& outComplete);
