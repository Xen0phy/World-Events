#pragma once
#include <chrono>
#include <vector>
#include <string>
#include <cstdint>

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
// oldName isn't currently subscribed. Also patches the toast-enabled list
// below, so a rename doesn't silently drop a "notify" setting the user
// already configured for that event.
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName);

// ---------------------------------------------------------------------------
// Per-event toast opt-in ("notify level")
// ---------------------------------------------------------------------------
// A subscribed Basic Event / Cyclic slot may ALSO opt into a "starting
// soon"/"now active" toast popup (subscriptions_notification.cpp) rather
// than just sitting quietly on the bar/window — most subscriptions are
// the former by default (someone building a big watchlist doesn't want a
// toast for every single one of them), so this is tracked as a second,
// independent membership list layered on top of the subscribed-list
// above, not a property of being subscribed at all.
//
// Modeled as one 0/1/2 "notify level" rather than two independent bools,
// since it's a strict ladder in the options tree's UI (see
// DrawNotifyLevelIcon in addon_options.cpp): 0 = unsubscribed, 1 =
// subscribed/silent, 2 = subscribed + toast. Level 2 always implies
// subscribed; Set...NotifyLevel(..., 0) clears BOTH lists together so
// there's no way to end up "toast-enabled but not subscribed" left over
// from an earlier configuration. A sound level (3) is intentionally not
// implemented yet — see the design discussion this shipped from.
//
// Get...NotifyLevel derives its answer from the subscribed/toast lists
// live (not its own separate piece of state), so it can never disagree
// with IsBasicEventSubscribed/IsBasicEventToastEnabled.
extern std::vector<std::string>            g_ToastEnabledBasicEvents;
extern std::vector<CyclicSubscriptionKey>  g_ToastEnabledCyclicSlots;

bool IsBasicEventToastEnabled(const std::string& eventName);
bool IsCyclicSlotToastEnabled(const CyclicSubscriptionKey& key);

int  GetBasicEventNotifyLevel(const std::string& eventName);
void SetBasicEventNotifyLevel(const std::string& eventName, int level); // clamped to 0..2

int  GetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key);
void SetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key, int level); // clamped to 0..2

// Bumped by exactly 1 on every change to g_SubscribedBasicEvents/
// g_SubscribedCyclicSlots — ToggleBasicEventSubscription,
// ToggleCyclicSlotSubscription, RenameSubscribedBasicEvent, and
// LoadSubscriptionsData. Lets subscriptions_cache.cpp cheaply detect "the
// subscribed set itself changed" (as opposed to just its members' active/
// completion state) without re-deriving anything to find out.
//
// NOT bumped by toast-enabled-list changes alone (Set...NotifyLevel when
// only the toast half changes, i.e. 1<->2) — nothing in
// subscriptions_cache.cpp reads the toast list, so there's nothing for a
// cache rebuild to pick up; subscriptions_notification.cpp reads it
// directly and unconditionally every frame instead (see CollectCandidates).
uint64_t GetSubscriptionListGeneration();

// Persists/loads g_SubscribedBasicEvents, g_SubscribedCyclicSlots,
// g_ToastEnabledBasicEvents, and g_ToastEnabledCyclicSlots as four
// top-level keys ("subscribedBasicEvents", "subscribedCyclicSlots",
// "toastEnabledBasicEvents", "toastEnabledCyclicSlots") in the same
// events.json file used by g_Events/g_CyclicGroups/categories. Call
// SaveEventsData() before SaveSubscriptionsData(), since this reads the
// file and writes it back. Both swallow exceptions and return false on
// failure.
bool SaveSubscriptionsData(const std::string& addonDir);
bool LoadSubscriptionsData(const std::string& addonDir);

// Sends `message` to whatever window is focused, via Enter -> Ctrl+V
// (paste) -> Enter, simulated on a detached background thread. Uses a mix
// of SendMessage (Enter/V) and SendInput (Ctrl); guarded by
// `send_in_progress` against overlapping calls.
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms);

// Builds the actual text PasteToChat above sends for a watchlist row/
// segment/toast click: "<name>: <chatCode>" (or just <name> if there's no
// chat code), with the user's configured channel command
// (Settings::ChatChannelPrefix, e.g. "/p ") stuck on the front so the
// paste lands in that channel no matter which one currently has focus in
// GW2's chat box. Prefix defaults to empty (no channel switch — pastes
// into whatever's already selected, same behavior as before this
// setting existed). Shared by subscriptions_window.cpp,
// subscriptions_bar.cpp, and subscriptions_notification.cpp so the
// prefixing logic lives in exactly one place.
std::string BuildChatPasteMessage(const std::string& name, const std::string& chatCode);

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
// Both are gated behind NotificationsEnabled — the master switch mentioned
// in both settings above — AND, for manually subscribed items, that
// specific event/slot's own toast opt-in (IsBasicEventToastEnabled/
// IsCyclicSlotToastEnabled below): most subscriptions default to silent,
// so building a big watchlist doesn't mean a toast for every single one
// of them. Auto-tracked weekly targets (not manually subscribed) aren't
// affected by that per-event flag. Clicking a popup pastes its waypoint
// code exactly like a row in the watchlist window / a segment on the
// distribution bar (see PasteToChat in subscriptions.cpp).
//
// Like the window and bar, this also surfaces active-and-incomplete weekly
// Wizard's Vault targets (weekly_vault.h) that aren't manually subscribed,
// each drawn with an extra thin red border — gated behind
// WeeklyAutoTrackEnabled (settings_table.h), the shared master switch for
// that auto-tracking overlay across all three views.
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