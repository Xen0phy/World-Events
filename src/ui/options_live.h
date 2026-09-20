//################################################################################
// options_live.h
//--------------------------------------------------------------------------------
// DrawOptionsLive               Live tab content
// RenderLiveEventButtons        upper-right corner report button(s)
// RenderLiveEventReportsWindow  draws the reports window; no-op unless open
// OpenLiveEventReportsWindow    opens the reports window
// kLiveEventReportsWindowId     ImGui ID of the reports window
//--------------------------------------------------------------------------------
// The Live tab: content pane of the rail's third tab. Top to bottom: the live-
// event switch with its key/region warnings and a link to the Help tab's
// explainers; the display toggles (Move button, reports window and its lock, map
// locations, name sharing, report color); and one table of every compiled-in live
// event with subscribe, "Only named" and "Done today" columns. A Live deep link
// scrolls to its row and flashes it.
//
// Only DrawOptionsLive runs through the settings window's tab dispatch. The two
// Render* functions are the in-game overlay (see options_live.cpp): AddonRender
// calls both every frame from its IsGameplay block (addon.cpp), gameplay-gated
// like the Subscriptions views (subscriptions_ui.h), whether or not the window or
// the tab is open.
//
// ShowLiveEventReportsWindow (settings_table.h) is itself a SETTING, so the
// reports window's visibility survives a restart and can also be toggled from the
// Live tab, independent of proximity to any event.
//--------------------------------------------------------------------------------

#pragma once

#include "options_window.h" //. OptionsDeepLink

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsLive
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected. link is non-null for exactly one frame after a Live deep link: the
// section then scrolls to the row and calls OptionsHighlight_Set
// (options_window.h).
//--------------------------------------------------------------------------------
void DrawOptionsLive(const OptionsDeepLink* link);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons
//--------------------------------------------------------------------------------
// No-op outside gameplay (no MumbleLink/NexusLink, or NexusLink->IsGameplay
// false). While the Live tab's Move button box is ticked, draws one draggable
// placeholder at LiveEventButtonMarginX/Y (settings_table.h) so its position is
// visible and adjustable without being subscribed or near an event - nothing else
// below applies while that's on. Otherwise a no-op unless LiveEventsSubscribed
// (settings_table.h) is set - no API key required (see gw2_api.h). Otherwise
// draws one borderless button per compiled-in LiveEvent (events_live.h) the
// player is within range of, stacked top-down from that anchor point. Left-click
// reports and opens the reports window; right-click just opens it.
//--------------------------------------------------------------------------------
void RenderLiveEventButtons();

//_ Untranslated on purpose (see WE_LIVE_REPORTS_WINDOW_TITLE, localization_table.h) - the "###" drops everything before it from ImGui's ID hash, so this stays stable across languages.
inline constexpr const char* kLiveEventReportsWindowId = "###WorldEventsLiveReports";

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
// Connected/Connecting/Disconnected" line, with a "(N online in EU/NA)" suffix
// from GetRegionViewerCount() (notification_client.h) whenever available - then
// one row per g_LiveEvents (events_live.h) entry on the player's current map, no
// per-event selection needed. Each row is the event's name plus the shard's last
// IPv4 octet (GetShardLastAddressOctet, shard_id.h), followed by "(empty)" or how
// long ago the most recent of the last 10 GetRecentReports(eventId) came in; more
// than one report folds into a collapsed tree node. No-op if
// ShowLiveEventReportsWindow (settings_table.h) is false. Call once per frame,
// alongside RenderLiveEventButtons - see the file header.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow();
