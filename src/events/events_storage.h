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
#include <vector>

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
// RestoreMissingDefaults
//--------------------------------------------------------------------------------
// Non-destructive counterpart to ResetEventsToDefaults: re-adds any compiled-in
// Basic Event, Cyclic Group, or Cyclic Slot currently missing from g_Events/
// g_CyclicGroups - the exact same "a missing default is always resurrected" rule
// LoadEventsData already applies via MergeByKey/MergeGroups on every startup -
// without discarding or altering anything already present. An unmodified default,
// a renamed/edited default, and a player-added entry all pass through completely
// untouched; only a genuinely missing default gets appended back (at the end of
// its list, not its original position). No-op (returns 0) if LoadEventsData never
// ran. Returns the number of entries added back, for the Events tab to report to
// the player - see DrawRestoreMissingButton, reset_defaults.cpp.
//--------------------------------------------------------------------------------
int RestoreMissingDefaults();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetDefaultEvent / GetDefaultCyclicGroup / GetDefaultCyclicSlot
//--------------------------------------------------------------------------------
// Look up a single entry in the same compiled-in snapshot ResetEventsToDefaults
// restores from, keyed by id the same way MergeByKey/MergeGroups match on load.
// Returns nullptr if the snapshot hasn't been captured yet, or no compiled-in
// entry has that id (a purely user-added event/group/slot) - the per-row "Reset"
// menu item in options_events_rows.cpp uses that to disable itself.
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
// Neither -> WE_UNNAMED. The Slot overload takes groupId separately since
// Slot::id is only unique within its group. DisplayNameEnglish always resolves to
// English regardless of active language, for code that must match ArenaNet's own
// API text or an old English-only save (weekly_vault.cpp, the
// eventNameToId/groupNameToId migrations).
//--------------------------------------------------------------------------------
const char* DisplayName(const WorldEvent& ev);
const char* DisplayNameEnglish(const WorldEvent& ev);

const char* DisplayName(const CyclicGroup& grp);
const char* DisplayNameEnglish(const CyclicGroup& grp);

const char* DisplayName(const CyclicGroup::Slot& slot, const std::string& groupId);
const char* DisplayNameEnglish(const CyclicGroup::Slot& slot, const std::string& groupId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SlugifyName / UniqueId / NewUniqueId
//--------------------------------------------------------------------------------
// Id-assignment helpers. A brand-new WorldEvent/CyclicGroup/CyclicGroup::Slot or
// Category gets its id up front, right when the options panel's "+" button
// creates it (see options_events.cpp/options_events_rows.cpp) - the id is never
// derived from the (still-unset) name. LoadEventsData also calls SlugifyName and
// UniqueId as a one-time backfill for any id left empty by a save from before
// that assignment existed (see the .cpp). NewUniqueId is the "+" side: an id
// unused among items (any element type with an id). The seed is a fixed ASCII
// word, since SlugifyName reduces a non-ASCII display default to nothing.
//--------------------------------------------------------------------------------
std::string SlugifyName(const std::string& name);
std::string UniqueId(const std::string& candidate, std::unordered_set<std::string>& used);

template <typename Item>
std::string NewUniqueId(const std::string& seed, const std::vector<Item>& items)
{
    std::unordered_set<std::string> used;
    for (const Item& item : items) used.insert(item.id);
    return UniqueId(SlugifyName(seed), used);
}
