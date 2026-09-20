//################################################################################
// subscriptions_edit_window.h
//--------------------------------------------------------------------------------
// ShowEditSubscriptionsWindow    transient visibility flag (see below)
// OpenEditSubscriptionsWindow()  open with no particular row targeted
// OpenEditSubscriptionsWindow(kind, basicId, cyclicKey, liveEventId)
//                                 open with that row already expanded
// RenderEditSubscriptionsWindow  draws the window; no-op unless open
//--------------------------------------------------------------------------------
// Standalone "quick access" window for subscription state only (notify level,
// done-for-today) - a lean, read-through view over the same Basic Event / Cyclic
// slot data as the main options panel's Table 3 (addon_options.cpp), with none of
// that panel's structural editing (add/remove/rename, drag-and-drop, coordinates,
// icon/color pickers, chat codes). One tab, "Basic & Cyclic" (the original two-
// column view); Live Events are not handled here - they are all-or-nothing
// subscribe/unsubscribe rows on the settings window's Live tab (options_live.h).
// Reached via the "Edit Subscriptions" entry in the bar segment / window row /
// toast right-click popups, plus a background right- click on the bar strip and
// the window's empty content area - see
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

//_ Untranslated on purpose (see WE_EDIT_SUBS_WINDOW_TITLE, localization_table.h) - the "###" drops everything before it from ImGui's ID hash, so this stays stable across languages. Shared between the ImGui::Begin() call in RenderEditSubscriptionsWindow and the APIDefs->GUI_RegisterCloseOnEscape/GUI_DeregisterCloseOnEscape calls in addon.cpp
inline constexpr const char* kEditSubscriptionsWindowId = "###WorldEventsEditSubscriptions";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// No-argument overload: opens the window with nothing pre-expanded - the
// background right-click entry point (bar strip / window empty area).
//
// Four-argument overload: opens the window and, on the very next draw, expands
// the row identified by (kind, basicId, cyclicKey) - and, for a Cyclic slot, its
// enclosing group too. Same identity trio already threaded through
// LineSegment/Row/Popup in subscriptions_bar.cpp/subscriptions_window.cpp/
// subscriptions_notification.cpp. For kind Live it opens nothing here: it
// forwards to OpenOptionsWindow (options_window.h), which scrolls to liveEventId
// on the Live tab. liveEventId defaults to empty, meaningful only for Live.
//--------------------------------------------------------------------------------
void OpenEditSubscriptionsWindow();
void OpenEditSubscriptionsWindow(SubscriptionKind kind, const std::string& basicId,
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