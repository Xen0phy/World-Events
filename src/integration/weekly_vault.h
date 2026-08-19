//################################################################################
// weekly_vault.h
//--------------------------------------------------------------------------------
// CyclicWeeklyTarget          one Cyclic slot that can satisfy an objective
// CyclicWeeklyMapping         one Cyclic objective + everything that satisfies it
// g_CyclicWeeklyObjectives    the editable Cyclic objective table
// IsBasicEventWeeklyTarget / IsCyclicSlotWeeklyTarget   weekly-target queries
//--------------------------------------------------------------------------------
// Wizard's Vault WEEKLY objectives, cross-referenced against this addon's Basic
// Events / Cyclic Group slots.
//
// Completely separate system from, and independent of, the DAILY
// worldbosses/mapchests tracking in gw2_api.h / WorldEvent::apiWorldBossId /
// CyclicGroup::apiMapChestId (events.h). The two can overlap on the same physical
// boss/meta (e.g. killing Tequatl satisfies both the daily worldbosses list AND,
// in a week the Vault rotation includes it, this week's weekly objective) but are
// tracked on entirely separate reset schedules and separate live API calls (see
// gw2_api.h's GetLiveWeeklyObjectives, GET /v2/account/wizardsvault/weekly).
//
// Two different matching strategies, since the two kinds of objective have very
// different title shapes:
//
// - Core Boss objectives ("Defeat the X World Boss or Complete Events in
//   Y") always embed the boss's own display name verbatim, so any
//   WorldEvent with a non-empty apiWorldBossId (events.h) is automatically
//   checked by name against whatever's live this week - no table needed,
//   and a newly-rotated-in boss needs zero edits anywhere in this addon.
// - Cyclic (meta-event) objectives ("...Events in Cantha or Events in
//   Maguuma Jungle") don't embed anything recognizable about the actual
//   meta/map, so g_CyclicWeeklyObjectives below is a hand-maintained table
//   instead - see CyclicWeeklyMapping for how it's matched.
//--------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

//********************************************************************************
// CyclicWeeklyTarget
//--------------------------------------------------------------------------------
// groupName    CyclicGroup::name
// slotName     CyclicGroup::Slot::name within that group
//--------------------------------------------------------------------------------
// One Cyclic Group slot that can satisfy a Cyclic weekly objective.
//--------------------------------------------------------------------------------
struct CyclicWeeklyTarget
{
    std::string groupName;
    std::string slotName;
};

//********************************************************************************
// CyclicWeeklyMapping
//--------------------------------------------------------------------------------
// titleKeywords    substrings matched case-insensitively against a live
//                  objective's title; ALL must appear in the SAME title
// targets          OR list - satisfying ANY ONE completes the objective
//--------------------------------------------------------------------------------
// One Cyclic Wizard's Vault objective and every target that can satisfy it. Two
// short, distinctive region names from the objective's own "Events in <region>"
// clauses are normally enough to identify it uniquely, without needing the full
// literal sentence and without breaking if ArenaNet tweaks wording elsewhere in
// the title. Safe even when two different mappings share one region name (e.g.
// two objectives both mentioning "Orr") - each live title is checked against one
// mapping's keywords at a time.
//
// targets is an OR list: several real objectives span multiple, unrelated
// maps/chains at once, and completing ANY ONE of the listed targets completes the
// objective in-game. The live API only reports the objective's own aggregate
// progress, not which specific target was responsible - so once this objective is
// Complete, EVERY target listed here stops being treated as an active weekly
// target, regardless of which one the player actually did.
//--------------------------------------------------------------------------------
struct CyclicWeeklyMapping
{
    std::vector<std::string>       titleKeywords;
    std::vector<CyclicWeeklyTarget> targets;
};

//_ THE editable Cyclic objectives table (see CyclicWeeklyMapping above);
// check_weekly_vault.py validates every target at build time.
extern std::vector<CyclicWeeklyMapping> g_CyclicWeeklyObjectives;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventWeeklyTarget / IsCyclicSlotWeeklyTarget
//--------------------------------------------------------------------------------
// True if the given Basic Event (by name) / Cyclic Group slot (by group + slot
// name) is a Core Boss or Cyclic objective, respectively, worth auto-tracking /
// showing a "weekly" marker for right now. outComplete is only meaningful when
// the function returns true: it reports whether that objective has already been
// completed this week. Callers (subscriptions_bar.cpp / subscriptions_window.cpp)
// decide what to do with a completed-but-still-manually-subscribed event - these
// functions only report the raw weekly state.
//--------------------------------------------------------------------------------
bool IsBasicEventWeeklyTarget(const std::string& eventName, bool& outComplete);
bool IsCyclicSlotWeeklyTarget(const std::string& groupName, const std::string& slotName, bool& outComplete);