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
#include <unordered_set>

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
// Otherwise, a compiled-in row (GetDefaultEvent/GetDefaultCyclicGroup/
// GetDefaultCyclicSlot) resolves to the WE_NAME_BASIC_<id>/WE_NAME_GROUP_<id>/
// WE_NAME_SLOT_<groupId>_<id> identifier (event_names.csv) via Tr()/ TrEnglish().
// Neither -> WE_UNNAMED, subscriptions_edit_window.cpp's existing fallback. The
// Slot overload takes groupId separately since Slot::id is only unique within its
// group. DisplayNameEnglish always resolves to English regardless of active
// language, for code that must match ArenaNet's own API text or an old English-
// only save (weekly_vault.cpp, the eventNameToId/groupNameToId migrations).
//--------------------------------------------------------------------------------
const char* DisplayName(const WorldEvent& ev);
const char* DisplayNameEnglish(const WorldEvent& ev);

const char* DisplayName(const CyclicGroup& grp);
const char* DisplayNameEnglish(const CyclicGroup& grp);

const char* DisplayName(const CyclicGroup::Slot& slot, const std::string& groupId);
const char* DisplayNameEnglish(const CyclicGroup::Slot& slot, const std::string& groupId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SlugifyName / UniqueId
//--------------------------------------------------------------------------------
// Migration-only id-backfill helpers, shared with events_categories.cpp's own
// pre-id Category migration. See the .cpp for what each does.
//--------------------------------------------------------------------------------
std::string SlugifyName(const std::string& name);
std::string UniqueId(const std::string& candidate, std::unordered_set<std::string>& used);