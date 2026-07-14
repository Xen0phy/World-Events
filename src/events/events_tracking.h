#pragma once
#include <string>
#include <cstdint>
#include "subscriptions.h" // CyclicSubscriptionKey — same (groupName, slotOffset) key shape

// ---------------------------------------------------------------------------
//events_tracking.h
// ---------------------------------------------------------------------------
// User-set "done for today" flags — a manual, local-only supplement to
// IsWorldBossCompletedToday/IsMapChestClaimedToday (gw2_api.h).
//
// Those two only cover the 13 Core Tyria world bosses and the 8 HoT/PoF
// map-chest maps, and only for someone with a working API key connected.
// Everything else in the roster — and everyone without a key — has no
// "already did this today" signal at all otherwise. This module fills
// that gap with a plain manual toggle, set via right-click in the
// subscriptions window/bar/toast (see those .cpp files).
//
// Deliberately independent of API state: a manually-marked event and an
// API-confirmed one are checked side by side at each call site (see
// subscriptions_window.cpp/subscriptions_bar.cpp/subscriptions_notification.cpp),
// each hiding the row on its own — this module has no idea whether an
// event even has an apiWorldBossId/apiMapChestId, and doesn't need to.
//
// Resets at UTC daily reset, same boundary gw2_api.cpp's CurrentUtcDay()
// uses — checked lazily (on read and on load), not on a timer, so there's
// no risk of a missed frame leaving a stale mark past reset.
// ---------------------------------------------------------------------------

bool IsBasicEventMarkedDoneToday(const std::string& eventName);
void ToggleBasicEventDoneToday(const std::string& eventName);

bool IsCyclicSlotMarkedDoneToday(const CyclicSubscriptionKey& key);
void ToggleCyclicSlotDoneToday(const CyclicSubscriptionKey& key);

// Clears every manual mark immediately, regardless of today's UTC day —
// the "reset all manual markers" button in the options panel. Does not
// touch API-derived completion state (that's gw2_api.cpp's own cache and
// isn't manual data).
void ClearAllDoneMarkers();

// Bumped by exactly 1 on every actual change to the done-today marks —
// ToggleBasicEventDoneToday, ToggleCyclicSlotDoneToday, ClearAllDoneMarkers,
// LoadDailyTrackingData (when it actually loads marks for today), and the
// UTC-day rollover inside RollOverIfNewUtcDay (when it actually clears
// yesterday's marks). Lets subscriptions_cache.cpp cheaply detect "a
// doneToday flag may have changed" without re-deriving anything to find
// out.
uint64_t GetDoneMarkersGeneration();

// Persisted in events.json alongside subscriptions, as two more sibling
// top-level keys ("doneTodayBasicEvents", "doneTodayCyclicSlots") plus the
// stored UTC day number they're valid for ("doneTodayUtcDay"). Call after
// SaveSubscriptionsData/before nothing in particular — order relative to
// that call doesn't matter, both just read-modify-write the same file.
// Both swallow exceptions and return false on failure, same as
// Save/LoadSubscriptionsData.
bool SaveDailyTrackingData(const std::string& addonDir);
bool LoadDailyTrackingData(const std::string& addonDir);
