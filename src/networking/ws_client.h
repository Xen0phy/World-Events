//################################################################################
// ws_client.h
//--------------------------------------------------------------------------------
// EventReport          one report as stored/read locally: {event_id, ts}
// WsConnectionState     Disconnected / Connecting / Connected
// InitWsClient()        starts the background connection thread; call once,
//                       from AddonLoad
// UpdateShard(shard)    call periodically (e.g. once/sec) with the current
//                       ComputeShardIdentity() result - (re)connects on change
// SendReport(eventId)   fire-and-forget a report for the current shard
// GetRecentReports(id)  thread-safe read of the last-10 buffer for one event
// GetConnectionState()  for UI feedback (e.g. disabling the report button)
//--------------------------------------------------------------------------------
// One persistent WebSocket connection, scoped to a single shard at a time (see
// shard_id.h). Built directly on WinHTTP's WebSocket API, already a hard
// dependency of this DLL (gw2_api.cpp), so no third-party WS library is added.
//
// Server is the timestamp authority: the client only ever sends event_id; the
// server stamps and echoes back {event_id, ts} to everyone connected, including
// the sender. This sidesteps client clock-skew entirely.
//
// AddonUnload MUST call ShutdownWsClient() in addition to the existing
// WaitForBackgroundThreads(2000) - see ShutdownWsClient below for why a polled
// wait alone isn't a strong enough guarantee for this thread.
//
// Built on WinHTTP's asynchronous mode (WINHTTP_FLAG_ASYNC) - see ws_client.cpp's
// file header for why that's the only mode where cancelling a pending WebSocket
// receive from another thread is documented as safe.
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
// eventId          matches whatever id scheme events.h ends up using for
//                   live-reportable events
// timestampUnix    server-stamped seconds since epoch (UTC) - see file header
//--------------------------------------------------------------------------------
struct EventReport
{
    std::string eventId;
    int64_t     timestampUnix = 0;
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
// Sends {"type":"report","event_id":eventId} on the current connection. No-op
// (report dropped) if not currently connected - see file header limitation note.
// Safe to call from any thread, including the render thread from a button's on-
// click.
//--------------------------------------------------------------------------------
void SendReport(const std::string& eventId);

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