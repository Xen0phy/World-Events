//################################################################################
// live_events_ui.h
//--------------------------------------------------------------------------------
// RenderLiveEventButtons        upper-right corner report button(s)
// LiveEventButtonMoveMode       transient "drag to reposition" flag (see below)
// OpenLiveEventReportsWindow    opens the window; see below for what it shows
// RenderLiveEventReportsWindow  draws the window; no-op unless open
//--------------------------------------------------------------------------------
// The UI half of the live-event-reporting feature (networking-handoff.md), the
// last piece section 9 of that handoff calls out as not yet built. Two views: a
// per-event button stack (see RenderLiveEventButtons below) and a popup listing
// recent reports for whichever button was last pressed (see
// RenderLiveEventReportsWindow below).
//
// ShowLiveEventReportsWindow (settings_table.h) is itself a SETTING, so the
// popup's visibility survives a restart and can also be toggled directly from the
// options panel, independent of proximity to any event - unlike
// LiveEventButtonMoveMode, which stays transient for the same reason
// ShowEditSubscriptionsWindow (subscriptions_edit_window.h) does: it's an editing
// mode, not state worth persisting.
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
// false). If LiveEventButtonMoveMode is true, draws one draggable placeholder at
// LiveEventButtonMarginX/Y (settings_table.h) so its position is visible and
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
// Opens the reports window (sets ShowLiveEventReportsWindow). Idempotent if
// already open - see RenderLiveEventReportsWindow for what it shows.
//--------------------------------------------------------------------------------
void OpenLiveEventReportsWindow();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventReportsWindow
//--------------------------------------------------------------------------------
// Draws live GetConnectionState() (ws_client.h) as a small "Server:
// Connected/Connecting/Disconnected" line, then one row per g_LiveEvents
// (events_live.h) entry on the player's current map - no per-event selection,
// entering a shard is enough. Each row is the event's name plus the shard's last
// IPv4 octet (GetShardLastAddressOctet, shard_id.h), followed by "(empty)" or how
// long ago the most recent of the last 10 GetRecentReports(eventId) came in; a
// row with more than one report folds into a tree node, collapsed by default,
// with the rest as leaves underneath. No-op if ShowLiveEventReportsWindow
// (settings_table.h) is false. Call once per frame, alongside
// RenderLiveEventButtons - see below.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow();