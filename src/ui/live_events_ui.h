//################################################################################
// live_events_ui.h
//--------------------------------------------------------------------------------
// RenderLiveEventButtons        upper-right corner report button(s)
// LiveEventButtonMoveMode       transient "drag to reposition" flag (see below)
// OpenLiveEventReportsWindow    open the window for one specific event
// RenderLiveEventReportsWindow  draws the window; no-op unless open
//--------------------------------------------------------------------------------
// The UI half of the live-event-reporting feature (networking-handoff.md), the
// last piece section 9 of that handoff calls out as not yet built. Two views: a
// per-event button stack (see RenderLiveEventButtons below) and a popup listing
// recent reports for whichever button was last pressed (see
// RenderLiveEventReportsWindow below).
//
// ShowLiveEventReportsWindow (settings_table.h) is itself a SETTING, so the
// popup's visibility survives a restart and can also be toggled directly from
// the options panel, independent of proximity to any event - unlike
// LiveEventButtonMoveMode, which stays transient for the same reason
// ShowEditSubscriptionsWindow (subscriptions_edit_window.h) does: it's an
// editing mode, not state worth persisting.
//
// Call both once per frame from AddonRender, gameplay-gated the same way as the
// Subscriptions views (subscriptions_ui.h) - see addon.cpp.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons
//--------------------------------------------------------------------------------
// No-op outside gameplay (no MumbleLink/NexusLink, or NexusLink->IsGameplay
// false). If LiveEventButtonMoveMode is true, draws one draggable placeholder
// at LiveEventButtonMarginX/Y (settings_table.h) so its position is visible and
// adjustable without being subscribed or near an event - nothing else below
// applies while that's on. Otherwise a no-op if LiveEventsSubscribed
// (settings_table.h) is false. Otherwise draws one borderless button per
// compiled-in LiveEvent (events_live.h) the player is within range of, stacked
// top-down from that same anchor point. Left-click sends a report and opens the
// reports window below; right-click opens it without reporting, to check what's
// already there.
//--------------------------------------------------------------------------------
void RenderLiveEventButtons();

//_ Transient only - see file header. Toggled from the "Move button" checkbox in addon_options.cpp; RenderLiveEventButtons reads it, no separate render function.
extern bool LiveEventButtonMoveMode;

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
// Draws live GetConnectionState() (ws_client.h) as a small "Server:
// Connected/Connecting/Disconnected" line, then the targeted event's name plus
// the shard's last IPv4 octet (GetShardLastAddressOctet, shard_id.h). Below
// that, up to the last 10 GetRecentReports(eventId): the most recent is a
// folded-by-default tree node labeled with how long ago it came in, and older
// ones are leaves underneath once unfolded. Placeholder text if no event has
// been targeted yet - possible now that ShowLiveEventReportsWindow can be
// turned on directly from the options panel. No-op if that flag is false. Call
// once per frame, alongside RenderLiveEventButtons - see AddonRender in
// addon.cpp.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow();