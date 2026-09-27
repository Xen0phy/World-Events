//################################################################################
// events_tracking.h
//--------------------------------------------------------------------------------
// IsBasicEventMarkedDoneToday / MarkBasicEventDoneToday / SetBasicEventDoneToday   Basic Event mark
// IsCyclicSlotMarkedDoneToday / MarkCyclicSlotDoneToday / SetCyclicSlotDoneToday   Cyclic slot mark
// IsLiveEventMarkedDoneToday / MarkLiveEventDoneToday / SetLiveEventDoneToday     Live Event mark
// ClearAllDoneMarkers        clears every manual mark immediately
// GetDoneMarkersGeneration   bumped on any actual change to the marks
// SaveDailyTrackingData / LoadDailyTrackingData   JSON persistence in
//                                                 events.json
//--------------------------------------------------------------------------------
// User-set "done for today" flags - a manual, local-only supplement to
// IsWorldBossCompletedToday/IsMapChestClaimedToday (gw2_api.h). Those two only
// cover the 13 Core Tyria world bosses and the 8 HoT/PoF map-chest maps, and only
// for someone with a working API key connected - everything else, and everyone
// without a key, has no "already did this today" signal otherwise. This module
// fills that gap with a plain manual mark, set via right-click in the
// subscriptions window/bar/toast (see those .cpp files) and toggleable both ways
// only from its own checkbox in the options panel (Set*DoneToday).
//
// Independent of API state: a manually-marked event and an API-confirmed one are
// checked side by side at each call site (see
// subscriptions_window.cpp/subscriptions_bar.cpp/
// subscriptions_notification.cpp), each hiding the row on its own - this module
// doesn't know or care whether an event even has an apiWorldBossId/apiMapChestId.
//
// Resets at UTC daily reset, same boundary gw2_api.cpp's CurrentUtcDay() uses -
// checked lazily (on read and on load), not on a timer, so no frame can leave a
// stale mark past reset.
//--------------------------------------------------------------------------------

#pragma once

//_ CyclicSubscriptionKey - same (groupId, slotId) key shape.
#include "subscriptions.h"

#include <string>
#include <cstdint>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventMarkedDoneToday / MarkBasicEventDoneToday / SetBasicEventDoneToday
//--------------------------------------------------------------------------------
// Query/mark the manual "done today" state for a Basic Event, by id. Mark is one-
// way (right-click "Mark done today"); Set takes an explicit bool, for the
// options panel's checkbox - the only place a mark is meant to clear outside the
// daily reset. Events sharing a WorldEvent::doneGroup (events.h) are
// marked/checked as one unit; see ResolveBasicDoneKey (events_tracking.cpp).
//--------------------------------------------------------------------------------
bool IsBasicEventMarkedDoneToday(const std::string& eventId);
void MarkBasicEventDoneToday(const std::string& eventId);
void SetBasicEventDoneToday(const std::string& eventId, bool done);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotMarkedDoneToday / MarkCyclicSlotDoneToday / SetCyclicSlotDoneToday
//--------------------------------------------------------------------------------
// Query/mark the manual "done today" mark for a Cyclic slot, by (groupId, slotId)
// key. Same one-way-Mark-vs-explicit-Set split as the Basic Event versions above.
//--------------------------------------------------------------------------------
bool IsCyclicSlotMarkedDoneToday(const CyclicSubscriptionKey& key);
void MarkCyclicSlotDoneToday(const CyclicSubscriptionKey& key);
void SetCyclicSlotDoneToday(const CyclicSubscriptionKey& key, bool done);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsLiveEventMarkedDoneToday / MarkLiveEventDoneToday / SetLiveEventDoneToday
//--------------------------------------------------------------------------------
// Query/mark the manual "done today" mark for a Live Event, by
// LiveEvent::eventId. No doneGroup indirection - unlike Basic Events, Live Events
// don't share payouts across roster entries. Same one-way-Mark-vs-explicit-Set
// split as the Basic Event versions above.
//--------------------------------------------------------------------------------
bool IsLiveEventMarkedDoneToday(const std::string& eventId);
void MarkLiveEventDoneToday(const std::string& eventId);
void SetLiveEventDoneToday(const std::string& eventId, bool done);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearAllDoneMarkers
//--------------------------------------------------------------------------------
// Clears every manual mark immediately, regardless of today's UTC day. Used by
// ResetAllDataToDefaults (addon.cpp); there is no button for it. Does not touch
// API-derived completion state (that's gw2_api.cpp's own cache, not manual data).
//--------------------------------------------------------------------------------
void ClearAllDoneMarkers();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetDoneMarkersGeneration
//--------------------------------------------------------------------------------
// Bumped by exactly 1 on every actual change to the done-today marks - the
// Mark*/Set* functions above, ClearAllDoneMarkers, LoadDailyTrackingData (when it
// actually loads marks for today), and the UTC-day rollover inside
// RollOverIfNewUtcDay (see events_tracking.cpp, when it actually clears
// yesterday's marks). Lets subscriptions_cache.cpp cheaply detect "a doneToday
// flag may have changed" without re-deriving anything to find out.
//--------------------------------------------------------------------------------
uint64_t GetDoneMarkersGeneration();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveDailyTrackingData / LoadDailyTrackingData
//--------------------------------------------------------------------------------
// Persisted in events.json alongside subscriptions, as three more sibling top-
// level keys ("doneTodayBasicEvents", "doneTodayCyclicSlots",
// "doneTodayLiveEvents") plus the stored UTC day number they're valid for
// ("doneTodayUtcDay"). Order relative to Save/LoadSubscriptionsData doesn't
// matter - both just read-modify-write the same file. Both swallow exceptions and
// return false on failure.
//--------------------------------------------------------------------------------
bool SaveDailyTrackingData(const std::string& addonDir);
bool LoadDailyTrackingData(const std::string& addonDir);