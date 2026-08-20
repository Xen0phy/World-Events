//################################################################################
// subscriptions_ui.h
//--------------------------------------------------------------------------------
// RenderSubscriptionsWindow         watchlist window of subscribed slots
// RenderSubscriptionsBar            colored 2h timeline bar of subscribed slots
// RenderSubscriptionsNotifications  toast popup stack for subscribed slots
//--------------------------------------------------------------------------------
// Three views over the same subscribed Basic Event / Cyclic Event slot data: a
// scrollable list, a colored 2-hour timeline bar, and transient corner toasts.
// Each view's visibility is gated by its own setting (ShowSubscriptionsWindow /
// ShowSubscriptionsBar / NotificationsEnabled, settings_table.h). All three read
// from subscriptions.h / subscriptions_cache.h and are meant to be called once
// per frame from AddonRender; call order between them does not matter since none
// of them mutate the underlying subscription data.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsWindow   (group: RenderSubscriptionsBar,
//                                     RenderSubscriptionsNotifications)
//--------------------------------------------------------------------------------
// Standalone window listing every subscribed slot, each with a live "Active" /
// "in Xm Ys" countdown. No-op if ShowSubscriptionsWindow is false.
//--------------------------------------------------------------------------------
void RenderSubscriptionsWindow();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsBar   (group: RenderSubscriptionsWindow,
//                                  RenderSubscriptionsNotifications)
//--------------------------------------------------------------------------------
// Draws each subscribed slot as a colored segment along a fixed 2-hour timeline
// (now..now+2h, not configurable). Segment color matches the slot's color
// elsewhere: CyclicGroup::SlotColor() for a Cyclic slot, the stable per-name
// color from BasicEventColorFor (subscriptions_bar.cpp) for a Basic Event. A
// "now" marker sits at the left edge; active segments are clipped to start there,
// and segments starting beyond the 2h window are not drawn. No-op if
// ShowSubscriptionsBar is false.
//--------------------------------------------------------------------------------
void RenderSubscriptionsBar();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsNotifications   (group: RenderSubscriptionsWindow,
//                                            RenderSubscriptionsBar)
//--------------------------------------------------------------------------------
// Draws and fires the notification popup stack: a "starting soon" toast
// NotificationLeadMinutes before a slot's next occurrence (0 = off), and an "it's
// live" toast the instant it starts, independently gated by NotificationOnStart.
// Both require NotificationsEnabled, plus - for manually subscribed items - that
// item's own toast opt-in (IsBasicEventToastEnabled / IsCyclicSlotToastEnabled,
// subscriptions.h); most default silent. Auto-tracked weekly Vault targets
// (weekly_vault.h) skip that per-item flag, getting a thin red border instead,
// controlled by WeeklyAutoTrackEnabled. Clicking a toast pastes its waypoint code
// (PasteToChat, subscriptions.cpp). No-op if NotificationsEnabled is false.
//--------------------------------------------------------------------------------
void RenderSubscriptionsNotifications();