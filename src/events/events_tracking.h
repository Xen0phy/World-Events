//################################################################################
// events_tracking.h
//--------------------------------------------------------------------------------
// IsBasicEventMarkedDoneToday / ToggleBasicEventDoneToday   Basic Event mark
// IsCyclicSlotMarkedDoneToday / ToggleCyclicSlotDoneToday   Cyclic slot mark
// ClearAllDoneMarkers        clears every manual mark immediately
// GetDoneMarkersGeneration   bumped on any actual change to the marks
// SaveDailyTrackingData / LoadDailyTrackingData   JSON persistence in
//                                                 events.json
//--------------------------------------------------------------------------------
// User-set "done for today" flags - a manual, local-only supplement to
// IsWorldBossCompletedToday/IsMapChestClaimedToday (gw2_api.h). Those two
// only cover the 13 Core Tyria world bosses and the 8 HoT/PoF map-chest
// maps, and only for someone with a working API key connected -
// everything else, and everyone without a key, has no "already did this
// today" signal otherwise. This module fills that gap with a plain
// manual toggle, set via right-click in the subscriptions window/bar/
// toast (see those .cpp files).
//
// Deliberately independent of API state: a manually-marked event and an
// API-confirmed one are checked side by side at each call site (see
// subscriptions_window.cpp/subscriptions_bar.cpp/
// subscriptions_notification.cpp), each hiding the row on its own - this
// module doesn't know or care whether an event even has an
// apiWorldBossId/apiMapChestId.
//
// Resets at UTC daily reset, same boundary gw2_api.cpp's CurrentUtcDay()
// uses - checked lazily (on read and on load), not on a timer, so no
// frame can leave a stale mark past reset.
//--------------------------------------------------------------------------------

#pragma once

//_ CyclicSubscriptionKey - same (groupName, slotOffset) key shape.
#include "subscriptions.h"

#include <string>
#include <cstdint>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventMarkedDoneToday / ToggleBasicEventDoneToday
//--------------------------------------------------------------------------------
// Query/toggle the manual "done today" mark for a Basic Event, by name.
//--------------------------------------------------------------------------------
bool IsBasicEventMarkedDoneToday(const std::string& eventName);
void ToggleBasicEventDoneToday(const std::string& eventName);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotMarkedDoneToday / ToggleCyclicSlotDoneToday
//--------------------------------------------------------------------------------
// Query/toggle the manual "done today" mark for a Cyclic slot, by
// (groupName, slotOffset) key.
//--------------------------------------------------------------------------------
bool IsCyclicSlotMarkedDoneToday(const CyclicSubscriptionKey& key);
void ToggleCyclicSlotDoneToday(const CyclicSubscriptionKey& key);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearAllDoneMarkers
//--------------------------------------------------------------------------------
// Clears every manual mark immediately, regardless of today's UTC day -
// the "reset all manual markers" button in the options panel. Does not
// touch API-derived completion state (that's gw2_api.cpp's own cache,
// not manual data).
//--------------------------------------------------------------------------------
void ClearAllDoneMarkers();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetDoneMarkersGeneration
//--------------------------------------------------------------------------------
// Bumped by exactly 1 on every actual change to the done-today marks -
// both Toggle* functions above, ClearAllDoneMarkers, LoadDailyTrackingData
// (when it actually loads marks for today), and the UTC-day rollover
// inside RollOverIfNewUtcDay (see events_tracking.cpp, when it actually
// clears yesterday's marks). Lets subscriptions_cache.cpp cheaply detect
// "a doneToday flag may have changed" without re-deriving anything to
// find out.
//--------------------------------------------------------------------------------
uint64_t GetDoneMarkersGeneration();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveDailyTrackingData / LoadDailyTrackingData
//--------------------------------------------------------------------------------
// Persisted in events.json alongside subscriptions, as two more sibling
// top-level keys ("doneTodayBasicEvents", "doneTodayCyclicSlots") plus
// the stored UTC day number they're valid for ("doneTodayUtcDay"). Order
// relative to Save/LoadSubscriptionsData doesn't matter - both just
// read-modify-write the same file. Both swallow exceptions and return
// false on failure.
//--------------------------------------------------------------------------------
bool SaveDailyTrackingData(const std::string& addonDir);
bool LoadDailyTrackingData(const std::string& addonDir);