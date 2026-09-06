//################################################################################
// subscriptions_edit_window.h
//--------------------------------------------------------------------------------
// ShowEditSubscriptionsWindow    transient visibility flag (see below)
// OpenEditSubscriptionsWindow()  open with no particular row targeted
// OpenEditSubscriptionsWindow(kind, basicName, cyclicKey, liveEventId)
//                                 open with that row already expanded
// RenderEditSubscriptionsWindow  draws the window; no-op unless open
//--------------------------------------------------------------------------------
// Standalone "quick access" window for subscription state only (notify level,
// done-for-today) - a lean, read-through view over the same Basic Event / Cyclic
// slot / Live Event data as the main options panel's Table 3 (addon_options.cpp),
// with none of that panel's structural editing (add/remove/rename, drag-and-drop,
// coordinates, icon/color pickers, chat codes). Two tabs: "Basic & Cyclic" (the
// original two-column view) and "Live Events" (flat list, no notify-level ladder
// - see live-toast-handoff.md section 6). Reached via the new "Edit
// Subscriptions" entry in the bar segment / window row / toast right-click
// popups, plus a background right-click on the bar strip and the window's empty
// content area - see
// subscriptions_bar.cpp/subscriptions_window.cpp/subscriptions_notification.cpp.
//
// ShowEditSubscriptionsWindow is NOT a persisted setting (contrast
// ShowSubscriptionsWindow/ShowSubscriptionsBar in settings_table.h): this window
// is meant as "pop it open, make a quick change, close it," not a standing
// overlay, so its visibility doesn't survive a restart and isn't written to
// events.json.
//--------------------------------------------------------------------------------

#pragma once

#include "subscriptions.h" //. CyclicSubscriptionKey

#include <string>

//_ Transient only - see file header for why this isn't a SETTING().
extern bool ShowEditSubscriptionsWindow;

//_ Shared between the ImGui::Begin() call in RenderEditSubscriptionsWindow and the APIDefs->GUI_RegisterCloseOnEscape/GUI_DeregisterCloseOnEscape calls in addon.cpp
inline constexpr const char* kEditSubscriptionsWindowTitle = "World Events - Edit Subscriptions";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// No-argument overload: opens the window with nothing pre-expanded - the
// background right-click entry point (bar strip / window empty area).
//
// Four-argument overload: opens the window, switches to the matching tab, and on
// the very next draw expands the row identified by (kind, basicName, cyclicKey)
// for Basic/Cyclic - and, for a Cyclic slot, its enclosing group too - or
// liveEventId for Live. Same identity trio (now three-way via SubscriptionKind,
// subscriptions.h) already threaded through LineSegment/Row/Popup in
// subscriptions_bar.cpp/subscriptions_window.cpp/subscriptions_notification.cpp;
// liveEventId defaults to empty, meaningful only when kind is Live.
//--------------------------------------------------------------------------------
void OpenEditSubscriptionsWindow();
void OpenEditSubscriptionsWindow(SubscriptionKind kind, const std::string& basicName,
    const CyclicSubscriptionKey& cyclicKey, const std::string& liveEventId = std::string());

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// Call once per frame (see AddonRender in addon.cpp) alongside
// RenderSubscriptionsWindow/Bar/Notifications. No-op if
// ShowEditSubscriptionsWindow is false. Unlike those three, not gated by the
// DisableWindowWhenCompetitive- style settings - those govern passive overlay
// visibility in PvP/WvW, not an editor the user just explicitly asked for.
//--------------------------------------------------------------------------------
void RenderEditSubscriptionsWindow();