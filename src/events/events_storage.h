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
// restores from, keyed by name the same way MergeByKey/MergeGroups match on load.
// Returns nullptr if the snapshot hasn't been captured yet, or no compiled-in
// entry has that name (a purely user-added event/group/slot, or one renamed away
// from its default name) - the per-row "Reset" menu item in
// addon_options_helpers.cpp uses that to disable itself.
//--------------------------------------------------------------------------------
const WorldEvent* GetDefaultEvent(const std::string& name);
const CyclicGroup* GetDefaultCyclicGroup(const std::string& name);
const CyclicGroup::Slot* GetDefaultCyclicSlot(const std::string& groupName, const std::string& slotName);