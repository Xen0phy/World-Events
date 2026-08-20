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
// Separate from, and independent of, the DAILY worldbosses/mapchests tracking in
// gw2_api.h / WorldEvent::apiWorldBossId / CyclicGroup::apiMapChestId (events.h).
// The two can overlap on the same physical boss/meta (e.g. killing Tequatl
// satisfies both the daily worldbosses list AND, in a week the Vault rotation
// includes it, this week's weekly objective) but are tracked on separate reset
// schedules and separate live API calls (see gw2_api.h's GetLiveWeeklyObjectives,
// GET /v2/account/wizardsvault/weekly).
//
// Two different matching strategies, since Core Boss and Cyclic objective titles
// are shaped very differently. See weekly_vault.cpp for how each is matched.
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
// clauses are normally enough to identify it uniquely, robust to ArenaNet
// rewording elsewhere in the title. Safe even when two mappings share one region
// name (e.g. two objectives mentioning "Orr") - each live title is checked
// against one mapping's keywords at a time.
//
// The live API reports only the objective's own aggregate progress, not which
// target was responsible - so once Complete, every target listed here stops being
// treated as an active weekly target, regardless of which one was done.
//--------------------------------------------------------------------------------
struct CyclicWeeklyMapping
{
    std::vector<std::string>       titleKeywords;
    std::vector<CyclicWeeklyTarget> targets;
};

//_ THE editable Cyclic table; check_weekly_vault.py validates targets at build time.
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