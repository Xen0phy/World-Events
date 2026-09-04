//################################################################################
// ws_client.h
//--------------------------------------------------------------------------------
// EventReport          one report as stored/read locally: {event_id, ts,
//                      reporter_name, region}
// WsConnectionState     Disconnected / Connecting / Connected
// InitWsClient()        starts the background connection thread; call once,
//                       from AddonLoad
// UpdateShard(shard)    call periodically (e.g. once/sec) with the current
//                       ComputeShardIdentity() result - (re)connects on change
// SendReport(id, name)  fire-and-forget a report for the current shard
// GetRecentReports(id)  thread-safe read of the last-10 buffer for one event
// GetConnectionState()  for UI feedback (e.g. disabling the report button)
//--------------------------------------------------------------------------------
// One persistent WebSocket connection, scoped to a single shard at a time (see
// shard_id.h). Built directly on WinHTTP's WebSocket API, already a hard
// dependency of this DLL (gw2_api.cpp), so no third-party WS library is added.
// Server is the timestamp authority: the client sends event_id/reporter_name/
// region, never a timestamp; the server stamps ts at receipt and echoes the whole
// thing back to everyone connected, including the sender - sidesteps client
// clock-skew entirely. reporter_name/region (live-toast-handoff.md sections 1/2)
// ride this same per-shard message; a report still only ever reaches this shard's
// own viewers - notification_client.h's separate connection handles region-wide
// toast delivery, fed by the relay in that same doc's section 3/8.
//
// AddonUnload MUST call ShutdownWsClient() in addition to the existing
// WaitForBackgroundThreads(2000) - a polled wait alone isn't a strong enough
// guarantee for this thread (see ShutdownWsClient below). Built on WinHTTP's
// asynchronous mode (WINHTTP_FLAG_ASYNC) - see ws_client.cpp's file header for
// why.
//
// Known v1 limitation: SendReport while disconnected drops the report - no
// outgoing retry queue.
//--------------------------------------------------------------------------------

#pragma once

#include "shard_id.h"

#include <cstdint>
#include <string>
#include <vector>

//********************************************************************************
// EventReport
//--------------------------------------------------------------------------------
// eventId          matches the id scheme events.h uses for live-reportable
//                  events
// timestampUnix    server-stamped seconds since epoch (UTC) - see file header
// reporterName     empty if sharing was off, or for a pre-upgrade broadcast
//                  (live-toast-handoff.md section 1; see HandleIncomingMessage)
// region           "NA"/"EU" as sent by the reporter, empty for the same reason
//                  as reporterName
//--------------------------------------------------------------------------------
struct EventReport
{
    std::string eventId;
    int64_t     timestampUnix = 0;
    std::string reporterName;
    std::string region;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WsConnectionState
//--------------------------------------------------------------------------------
// Mirrors connection lifecycle 1:1 - see GetConnectionState().
//--------------------------------------------------------------------------------
enum class WsConnectionState
{
    Disconnected,
    Connecting,
    Connected,
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitWsClient
//--------------------------------------------------------------------------------
// Spawns the single long-lived background connection thread
// (BackgroundThreadGuard- tracked, same shutdown story as every other background
// thread in this addon - see background_threads.h). Call exactly once, from
// AddonLoad. Idle (no connection attempted) until the first UpdateShard call with
// a valid shard.
//--------------------------------------------------------------------------------
void InitWsClient();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShutdownWsClient
//--------------------------------------------------------------------------------
// Call from AddonUnload, after WaitForBackgroundThreads(2000). Unlike that
// generic poll-with-timeout, this actually joins the connection thread and blocks
// until it has exited - unbounded on purpose, since returning control to Nexus
// while a thread still runs inside this DLL is worse than a longer unload. See
// ws_client.cpp for why the join is safe to leave unbounded.
//--------------------------------------------------------------------------------
void ShutdownWsClient();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UpdateShard
//--------------------------------------------------------------------------------
// Call periodically (once/sec is plenty - map transitions aren't instant, and
// this only does real work when the key actually changes) with the latest
// ComputeShardIdentity() result. A changed key reconnects; shard.valid == false
// (e.g. character select, loading screen) drops any active connection until a
// valid shard comes back. Safe to call from the render thread; cheap no-op when
// the key hasn't changed.
//--------------------------------------------------------------------------------
void UpdateShard(const ShardIdentity& shard);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SendReport
//--------------------------------------------------------------------------------
// Sends {"type":"report","event_id":eventId,"reporter_name":reporterName,
// "region":<GetLiveEventsRegion() as wire string>} on the current connection -
// region is derived internally at send time (GetLiveEventsRegion, gw2_api.h, is
// cheap enough to call on-demand), never a parameter. Pass reporterName ==
// GetMumbleCharacterName() when ShareNameInReports is on, "" when it's off (live-
// toast-handoff.md section 1) - this function doesn't read that setting, so the
// report-button call site stays the one place that decision is made. No-op if not
// currently connected - see file header limitation note. Safe to call from any
// thread, including the render thread from a button's on-click.
//--------------------------------------------------------------------------------
void SendReport(const std::string& eventId, const std::string& reporterName);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetRecentReports
//--------------------------------------------------------------------------------
// Returns up to the last 10 reports for one event on the CURRENT shard, newest
// first. Empty if none yet, or if not connected to any shard. Cheap enough to
// call every frame for whatever events are currently shown/subscribed.
//--------------------------------------------------------------------------------
std::vector<EventReport> GetRecentReports(const std::string& eventId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetConnectionState
//--------------------------------------------------------------------------------
// Snapshot of the connection lifecycle, for UI feedback (e.g. graying out the
// report button while Connecting, or showing a small status dot).
//--------------------------------------------------------------------------------
WsConnectionState GetConnectionState();