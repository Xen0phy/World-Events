//################################################################################
// notification_client.h
//--------------------------------------------------------------------------------
// LiveEventNotification   one region-wide report as received, unfiltered
// InitNotificationClient()        starts the background connection thread; call
//                                 once, from AddonLoad
// ShutdownNotificationClient()    call from AddonUnload, after
//                                 ShutdownWsClient()
// UpdateNotificationState(region) call periodically (e.g. once/sec) with the
//                                 current GetLiveEventsRegion() (gw2_api.h)
//                                 result
// DrainLiveEventNotifications()   thread-safe pop of every notification queued
//                                 since the last call
// GetNotificationConnectionState() for UI feedback
//--------------------------------------------------------------------------------
// Second, independent persistent WebSocket connection alongside ws_client.cpp's
// per-shard one - see live-toast-handoff.md sections 2/5. Kept in its own file:
// the shard connection follows map transitions every few minutes, this one
// follows world transfers, almost never, and the two share little else - no
// outgoing message (this connection only ever receives), no per-event history
// (late joiners just wait for the next live report), keyed by region ("EU"/"NA")
// instead of a per-shard hash. Built on the same WinHTTP asynchronous-mode
// pattern as ws_client.cpp, for the same reason; not shared code, the two
// connections' lifecycles differ enough that sharing would mostly add
// indirection.
//
// This module decides on its own whether it should be connected at all (see
// UpdateNotificationState), by reading g_SubscribedLiveEvents (subscriptions.h)
// and IsLiveEventMarkedDoneToday (events_tracking.h) directly - the same way
// subscriptions_cache.cpp derives its own state, instead of requiring a caller to
// pass that decision in. It does NOT decide which incoming notification is toast-
// worthy: DrainLiveEventNotifications returns every report the server sent,
// subscribed or not, done-today or not. That filtering is
// subscriptions_notification.cpp's job at toast-candidate collection time
// (section 6) - the same split ws_client.h's GetRecentReports leaves callers.
//--------------------------------------------------------------------------------

#pragma once

#include "gw2_api.h" //. LiveEventsRegion - the type UpdateNotificationState takes
#include "ws_client.h" //. WsConnectionState - same connection-lifecycle enum, shared rather than redeclared

#include <cstdint>
#include <string>
#include <vector>

//********************************************************************************
// LiveEventNotification
//--------------------------------------------------------------------------------
// eventId          matches LiveEvent::eventId (events_live.h)
// timestampUnix    server-stamped seconds since epoch (UTC)
// reporterName     empty if the reporter had sharing off (see
//                  live-toast-handoff.md section 1)
// mapId            GW2 map id the report was filed on - lets a caller show/skip
//                  a "which map" hint without a g_LiveEvents lookup
//--------------------------------------------------------------------------------
struct LiveEventNotification
{
    std::string eventId;
    int64_t     timestampUnix = 0;
    std::string reporterName;
    int         mapId = 0;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitNotificationClient
//--------------------------------------------------------------------------------
// Spawns the background connection thread (BackgroundThreadGuard-tracked, same
// shutdown story as every other background thread in this addon - see
// background_threads.h). Call exactly once, from AddonLoad. Idle (no connection
// attempted) until the first UpdateNotificationState call decides a connection is
// wanted.
//--------------------------------------------------------------------------------
void InitNotificationClient();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShutdownNotificationClient
//--------------------------------------------------------------------------------
// Call from AddonUnload, after WaitForBackgroundThreads(2000). Joins the
// connection thread and blocks until it has exited - see ws_client.h's
// ShutdownWsClient for why this is safe to leave unbounded.
//--------------------------------------------------------------------------------
void ShutdownNotificationClient();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UpdateNotificationState
//--------------------------------------------------------------------------------
// Call periodically (once/sec is plenty, same cadence as ws_client.h's
// UpdateShard) with the latest GetLiveEventsRegion() (gw2_api.h) result -
// LiveEventsRegion::Unknown covers character select, loading screens, no API key,
// and an unrecognized/not-yet-fetched home world alike (see gw2_api.h for why
// this is an enum, not a bare wire string). Internally re-derives "should a
// connection be open at all" from g_SubscribedLiveEvents/
// IsLiveEventMarkedDoneToday, cached against GetSubscriptionListGeneration()/
// GetDoneMarkersGeneration() (same pattern subscriptions_cache.cpp uses) - cheap
// enough to call every frame. No connection opens while every subscribed event is
// done today or the list is empty, regardless of region.
//--------------------------------------------------------------------------------
void UpdateNotificationState(LiveEventsRegion region);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrainLiveEventNotifications
//--------------------------------------------------------------------------------
// Returns every notification received since the last call, oldest first, then
// empties the internal queue - unlike ws_client.h's GetRecentReports (a
// repeatedly-readable snapshot), this is drain-once: each incoming report is a
// one-shot toast candidate (live-toast-handoff.md section 6), not standing state
// to re-read every frame. Safe to call from the render thread; returns empty when
// nothing new has arrived, including while disconnected.
//--------------------------------------------------------------------------------
std::vector<LiveEventNotification> DrainLiveEventNotifications();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetNotificationConnectionState
//--------------------------------------------------------------------------------
// Snapshot of the connection lifecycle, for UI feedback - same contract as
// ws_client.h's GetConnectionState.
//--------------------------------------------------------------------------------
WsConnectionState GetNotificationConnectionState();
