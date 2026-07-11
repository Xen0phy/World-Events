#pragma once
#include <chrono>
#include <vector>
#include <string>

// A user-picked watchlist of events, surfaced in a standalone window (see
// subscriptions_window.h). References existing data by name/key rather
// than owning a copy — the render/window code looks up the live
// WorldEvent/CyclicGroup::Slot in g_Events / g_CyclicGroups every frame.
//
// Basic Events are identified by name. Cyclic Events are subscribed to
// per slot (an individual occurrence within a group), keyed by (group
// name, slot offset) rather than (group name, slot name), since slot
// names aren't unique within a group but offsets are.
struct CyclicSubscriptionKey
{
    std::string groupName;
    int         slotOffset = 0;

    bool operator==(const CyclicSubscriptionKey& other) const
    {
        return groupName == other.groupName && slotOffset == other.slotOffset;
    }
};

// Subscribed Basic Events, referenced by WorldEvent::name.
extern std::vector<std::string> g_SubscribedBasicEvents;

// Subscribed Cyclic Event occurrences (individual slots), referenced by
// (group name, slot offset).
extern std::vector<CyclicSubscriptionKey> g_SubscribedCyclicSlots;

// Query/toggle helpers over the subscription lists above.
bool IsBasicEventSubscribed(const std::string& eventName);
void ToggleBasicEventSubscription(const std::string& eventName);

bool IsCyclicSlotSubscribed(const CyclicSubscriptionKey& key);
void ToggleCyclicSlotSubscription(const CyclicSubscriptionKey& key);

// Patches a Basic Event subscription from oldName to newName. No-op if
// oldName isn't currently subscribed.
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName);

// Persists/loads g_SubscribedBasicEvents and g_SubscribedCyclicSlots as
// two top-level keys ("subscribedBasicEvents", "subscribedCyclicSlots")
// in the same events.json file used by g_Events/g_CyclicGroups/
// categories. Call SaveEventsData() before SaveSubscriptionsData(), since
// this reads the file and writes it back. Both swallow exceptions and
// return false on failure.
bool SaveSubscriptionsData(const std::string& addonDir);
bool LoadSubscriptionsData(const std::string& addonDir);

// Sends `message` to whatever window is focused, via Enter -> Ctrl+V
// (paste) -> Enter, simulated on a detached background thread. Uses a mix
// of SendMessage (Enter/V) and SendInput (Ctrl); guarded by
// `send_in_progress` against overlapping calls.
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms);

// A standalone ImGui window listing every subscribed Basic Event and
// Cyclic Event slot, each with a live "Active" / "in Xm Ys" countdown.
// Visibility is controlled by ShowSubscriptionsWindow (settings_table.h).

// Draws the watchlist window if ShowSubscriptionsWindow is true; no-op
// otherwise. Call once per frame from AddonRender.
void RenderSubscriptionsWindow();

// A second view of the same subscription data as RenderSubscriptionsWindow:
// draws every subscribed Basic Event / Cyclic slot as a colored segment
// along a fixed 2-hour timeline bar instead of a scrollable text list.
//
// Each segment uses the same color the subscribed event/slot has
// elsewhere: a Cyclic slot uses CyclicGroup::SlotColor(), a Basic Event
// uses a stable per-name color (BasicEventColorFor in subscriptions_bar.cpp).
//
// The time range is fixed at 2 hours (now..now+2h) and not configurable.
// A "now" marker sits at the left edge; active segments are clipped to
// start at that marker, and segments starting beyond the 2h window are
// not drawn.
//
// Visibility is controlled by ShowSubscriptionsBar (settings_table.h).

// Draws the distribution bar if ShowSubscriptionsBar is true; no-op
// otherwise. Call once per frame from AddonRender, alongside
// RenderSubscriptionsWindow.
void RenderSubscriptionsBar();

// A third view of the same subscription data as RenderSubscriptionsWindow /
// RenderSubscriptionsBar (subscriptions.h): instead of a persistent list or
// strip, this fires small, transient "toast" popups in the lower-right
// corner of the screen —
//
//   - a "starting soon" popup, NotificationLeadMinutes before a subscribed
//     Basic Event or Cyclic slot's next occurrence begins (0 = off), and
//   - an "it's live" popup the instant that occurrence actually starts,
//     independently gated by NotificationOnStart (settings_table.h).
//
// Both are entirely gated behind NotificationsEnabled — the master switch
// mentioned in both settings above. Clicking a popup pastes its waypoint
// code exactly like a row in the watchlist window / a segment on the
// distribution bar (see PasteToChat in subscriptions.cpp).
//
// Unlike the window/bar, this has no "show/hide" checkbox of its own beyond
// NotificationsEnabled: a fired popup is transient by nature, so there's
// nothing to toggle the *visibility* of independent from the feature being
// on at all.

// Draws (and internally advances/fires) the notification popup stack.
// No-op if NotificationsEnabled is false. Call once per frame from
// AddonRender, alongside RenderSubscriptionsWindow/RenderSubscriptionsBar —
// order relative to those two doesn't matter, this only reads the same
// subscription/event data, it doesn't mutate it.
void RenderSubscriptionsNotifications();