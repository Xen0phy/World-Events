//################################################################################
// ws_debug_log.h
//--------------------------------------------------------------------------------
// WsLogDir               Info / Tx / Rx / Error - see WsLogEntry
// WsLogEntry             one log line: elapsed time, direction, text
// InitWsDebugLog()       resets the in-memory ring buffer
// ShutdownWsDebugLog()   no-op (see below)
// WsLog / WsLogf         appends one log line
// GetWsDebugLogSnapshot  copies the ring buffer for the debug window
// ClearWsDebugLog        empties the ring buffer
//--------------------------------------------------------------------------------
// Purpose: ws_client.cpp's async WinHTTP rewrite (v2, see that file's header)
// moved connect/send/receive off blocking calls and onto a status-callback state
// machine, which makes "why didn't an event show up" much harder to answer by
// reading the code alone. This module is a plain, dependency-free record of every
// step of that state machine - every connect attempt, every byte sent, every byte
// received, every error - independent of whether it ever reaches
// events_live.cpp/the UI. If a report goes out but never comes back, this log is
// where that turns from a guess into an observation: either the Rx line is
// missing entirely (server/network problem) or it's present and something after
// it (parsing, event_id matching, the UI's own read) is at fault. Every line is
// also mirrored into Nexus's own log (WsLog, ws_debug_log.cpp) at TRACE under
// "WorldEvents-WS" - survives an addon reload, cleared on the next game launch.
// One log for a user to send beats each addon keeping its own file.
//--------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WsLogDir
//--------------------------------------------------------------------------------
// Info: lifecycle/connection-state narration (connecting, connected, backoff,
// shard change, shutdown). Tx/Rx: raw message payloads, one entry per whole
// WebSocket message (not per fragment). Error: anything that ended a connection
// attempt or a receive/send early.
//--------------------------------------------------------------------------------
enum class WsLogDir
{
    Info,
    Tx,
    Rx,
    Error,
};

//********************************************************************************
// WsLogEntry
//--------------------------------------------------------------------------------
// sessionSec   seconds since this InitWsDebugLog call
// dir          Info/Tx/Rx/Error - see WsLogDir
// text         the log line itself
//--------------------------------------------------------------------------------
struct WsLogEntry
{
    double      sessionSec = 0.0;
    WsLogDir    dir        = WsLogDir::Info;
    std::string text;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitWsDebugLog / ShutdownWsDebugLog
//--------------------------------------------------------------------------------
// Call InitWsDebugLog once from AddonLoad, before InitWsClient - so the very
// first connect attempt is captured. ShutdownWsDebugLog is a no-op kept only so
// AddonUnload's call sites stay symmetric with AddonLoad's.
//--------------------------------------------------------------------------------
void InitWsDebugLog();
void ShutdownWsDebugLog();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WsLog / WsLogf
//--------------------------------------------------------------------------------
// Appends one line to the in-memory ring buffer and mirrors it to APIDefs->Log.
// Safe to call from any thread - internally mutex-guarded, since ws_client.cpp's
// background thread AND WinHTTP's own callback thread (StatusCallback, arbitrary
// WinHTTP-internal thread) both call this. Cheap enough to call on every
// message/state transition; not intended for a per-frame render-thread hot path.
//--------------------------------------------------------------------------------
void WsLog(WsLogDir dir, const std::string& text);
void WsLogf(WsLogDir dir, const char* fmt, ...);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetWsDebugLogSnapshot / ClearWsDebugLog
//--------------------------------------------------------------------------------
// GetWsDebugLogSnapshot copies the current ring buffer (oldest-first); cheap
// enough to call every frame the debug window is open, same contract as
// GetRecentReports elsewhere in this addon - no disk I/O, so there's nothing for
// that per-frame call to ever block on. ClearWsDebugLog empties it.
//--------------------------------------------------------------------------------
std::vector<WsLogEntry> GetWsDebugLogSnapshot();
void ClearWsDebugLog();