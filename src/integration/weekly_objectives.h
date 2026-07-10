#pragma once
#include <string>
#include <vector>

// ===========================================================================
// weekly_objectives.h
// ===========================================================================
// Curated mapping: Wizard's Vault WEEKLY objectives -> the specific
// WorldEvent / CyclicGroup+Slot in THIS addon's own data (events_basic.cpp /
// events_cyclic.cpp) that should be auto-tracked and marked with a red
// "weekly" dot in the Subscriptions bar/window while that objective is live
// and not yet complete.
//
// THIS FILE IS THE PLACE TO ADJUST THIS BY HAND. It is deliberately
// hand-curated, not derived from the API or from region/zone data,
// because several of the umbrella objectives below list many possible
// qualifying events across a whole region, but not all of them are worth
// a weekly dot. The canonical example (from the person who asked for
// this feature): "Battle in Tarir" is satisfied by completing Auric
// Basin's full multi-slot meta chain, but only Octovine — the actual
// climactic fight — is worth flagging; Auric Basin's other two slots
// (Challenges, Pylons) are setup/side content for that same fight, not a
// second thing to separately weekly-track. Every mapping below that picks
// one slot out of a multi-slot group follows the same principle — see the
// per-entry comments in weekly_objectives.cpp for which slot and why.
//
// ---------------------------------------------------------------------------
// Matching against the live API
// ---------------------------------------------------------------------------
// Each mapping's `objectiveTitle` is matched by EXACT text against
// GET /v2/account/wizardsvault/weekly's objectives[].title this week (see
// gw2_api.h's IsWeeklyObjectivePending). Wizard's Vault objective wording
// is only guaranteed stable WITHIN one season — if ArenaNet rewords an
// objective in a future season, its entry here simply stops matching
// (inert, not a crash: that entry's red dot/auto-track just stops
// appearing) until this file is updated with the new wording. This is a
// carried-over caveat from the original research note on this feature
// (see HANDOFF.md) — there is no more robust option than substring/exact
// title matching available from the public API today.
// ===========================================================================

// One Basic Event (world boss) weekly objective. WorldEvent::name is
// matched exactly (same convention as WorldEvent::apiWorldBossId).
struct WeeklyBasicTarget
{
    std::string objectiveTitle; // exact text from wizardsvault/weekly this season
    std::string eventName;      // WorldEvent::name in events_basic.cpp
};

// One target slot for a Cyclic (meta-event) weekly umbrella objective.
struct WeeklyCyclicTarget
{
    std::string groupName; // CyclicGroup::name
    std::string slotName;  // CyclicGroup::Slot::name within that group
};

// One regional umbrella objective ("Complete a Meta-Event or Events in
// X or Events in Y"), satisfied by ANY of several named events across
// (usually) more than one map. The API only exposes ONE progress/
// complete/claimed state for the whole umbrella — there's no way to tell
// which specific event contributed — so every target below shares the
// same live/complete state: while the objective is live this week and
// not yet complete, EVERY target here gets the weekly dot; the moment
// it's complete, they all stop at once.
struct WeeklyCyclicMapping
{
    std::string objectiveTitle;
    std::vector<WeeklyCyclicTarget> targets;
};

// The Basic Event (world boss) weekly objectives currently seen in the
// Wizard's Vault rotation. Populated in weekly_objectives.cpp.
extern std::vector<WeeklyBasicTarget> g_WeeklyBasicTargets;

// The regional Cyclic (meta-event) umbrella objectives. Populated in
// weekly_objectives.cpp.
extern std::vector<WeeklyCyclicMapping> g_WeeklyCyclicMap;
