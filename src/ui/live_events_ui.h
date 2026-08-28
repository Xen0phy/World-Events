//################################################################################
// live_events_ui.h
//--------------------------------------------------------------------------------
// RenderLiveEventButtons        upper-right corner report button(s)
// ShowLiveEventReportsWindow    transient visibility flag (see below)
// OpenLiveEventReportsWindow    open the window for one specific event
// RenderLiveEventReportsWindow  draws the window; no-op unless open
//--------------------------------------------------------------------------------
// The UI half of the live-event-reporting feature (networking-handoff.md), the
// last piece section 9 of that handoff calls out as not yet built. Two views: a
// per-event button stack (see RenderLiveEventButtons below) and a popup listing
// recent reports for whichever button was last pressed (see
// RenderLiveEventReportsWindow below).
//
// The popup is transient, not a SETTING (contrast ShowLiveEventButton in
// settings_table.h) - same reasoning as ShowEditSubscriptionsWindow
// (subscriptions_edit_window.h): a "press button, glance at what's up, close it"
// popup doesn't need to survive a restart.
//
// Call both once per frame from AddonRender, gameplay-gated the same way as the
// Subscriptions views (subscriptions_ui.h) - see addon.cpp.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons
//--------------------------------------------------------------------------------
// No-op if ShowLiveEventButton (settings_table.h) is false, or outside gameplay
// (no MumbleLink/NexusLink, or NexusLink->IsGameplay false). Otherwise draws one
// borderless button per activated LiveEvent the player is currently within range
// of, stacked top-down from the upper-right corner. Left-click sends a report for
// that event and opens the reports window below; right-click opens the reports
// window without sending one, so a player can check what's already been reported.
//--------------------------------------------------------------------------------
void RenderLiveEventButtons();

//_ Transient only - see file header for why this isn't a SETTING().
extern bool ShowLiveEventReportsWindow;

//_ Shared with addon.cpp's APIDefs->GUI_RegisterCloseOnEscape/GUI_DeregisterCloseOnEscape calls.
inline constexpr const char* kLiveEventReportsWindowTitle = "World Events — Live Reports";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenLiveEventReportsWindow
//--------------------------------------------------------------------------------
// Opens the reports window (sets ShowLiveEventReportsWindow) targeted at one
// event. Re-calling with a different eventId while the window is already open
// just retargets it - there's only ever one reports window, not one per event.
//--------------------------------------------------------------------------------
void OpenLiveEventReportsWindow(const std::string& eventId, const std::string& eventName);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventReportsWindow
//--------------------------------------------------------------------------------
// Draws the currently-targeted event's name, live GetConnectionState()
// (ws_client.h) as a small "Server: Connected/Connecting/Disconnected" line, and
// up to the last 10 GetRecentReports(eventId) as "reported Xm Ys ago", newest
// first (already the order GetRecentReports returns). No-op if
// ShowLiveEventReportsWindow is false. Call once per frame, alongside
// RenderLiveEventButtons - see AddonRender in addon.cpp.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow();