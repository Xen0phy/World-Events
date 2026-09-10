//################################################################################
// events_storage.h
//--------------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in
// "<addonDir>/events.json". See events_storage.cpp for the merge rules used on
// load.
//--------------------------------------------------------------------------------

#pragma once

#include "events.h"

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveEventsData / LoadEventsData
//--------------------------------------------------------------------------------
// Both functions swallow exceptions and return false on failure.
//--------------------------------------------------------------------------------
bool SaveEventsData(const std::string& addonDir);
bool LoadEventsData(const std::string& addonDir);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetEventsToDefaults
//--------------------------------------------------------------------------------
// Restores g_Events/g_CyclicGroups in memory to the exact compiled-in roster,
// snapshotted the first time LoadEventsData ran. No-op if LoadEventsData never
// ran. Doesn't touch disk - see ResetAllDataToDefaults (addon.h) for the full
// "Default" button sequence.
//--------------------------------------------------------------------------------
void ResetEventsToDefaults();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetDefaultEvent / GetDefaultCyclicGroup / GetDefaultCyclicSlot
//--------------------------------------------------------------------------------
// Look up a single entry in the same compiled-in snapshot ResetEventsToDefaults
// restores from, keyed by id the same way MergeByKey/MergeGroups match on load.
// Returns nullptr if the snapshot hasn't been captured yet, or no compiled-in
// entry has that id (a purely user-added event/group/slot) - the per-row "Reset"
// menu item in addon_options_helpers.cpp uses that to disable itself.
//--------------------------------------------------------------------------------
const WorldEvent* GetDefaultEvent(const std::string& id);
const CyclicGroup* GetDefaultCyclicGroup(const std::string& id);
const CyclicGroup::Slot* GetDefaultCyclicSlot(const std::string& groupId, const std::string& slotId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DisplayName / DisplayNameEnglish
//--------------------------------------------------------------------------------
// ev.customName non-empty -> that, literally (user override, never translated).
// customName empty and GetDefaultEvent(ev.id)/GetDefaultCyclicGroup(grp.id)/
// GetDefaultCyclicSlot(groupId, slot.id) finds a compiled-in row -> the
// WE_NAME_BASIC_<id>/WE_NAME_GROUP_<id>/WE_NAME_SLOT_<groupId>_<id> identifier
// (resources/localization/event_names.csv), through Tr()/TrEnglish()
// respectively. Neither -> WE_UNNAMED, same fallback
// subscriptions_edit_window.cpp already used before this set existed.
//
// The Slot overload takes groupId separately since Slot::id is only unique within
// its group - the identifier needs both.
//
// DisplayNameEnglish always resolves to the English text regardless of the active
// language - for anywhere the code must match ArenaNet's own English API text or
// an old English-only save file (weekly_vault.cpp's title matching, the
// eventNameToId/groupNameToId migrations in subscriptions.cpp/
// events_categories.cpp/events_tracking.cpp), never the player's current
// language.
//--------------------------------------------------------------------------------
const char* DisplayName(const WorldEvent& ev);
const char* DisplayNameEnglish(const WorldEvent& ev);

const char* DisplayName(const CyclicGroup& grp);
const char* DisplayNameEnglish(const CyclicGroup& grp);

const char* DisplayName(const CyclicGroup::Slot& slot, const std::string& groupId);
const char* DisplayNameEnglish(const CyclicGroup::Slot& slot, const std::string& groupId);