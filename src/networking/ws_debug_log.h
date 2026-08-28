//################################################################################
// ws_debug_log.h
//--------------------------------------------------------------------------------
// WsLogDir               Info / Tx / Rx / Error - see WsLogEntry
// WsLogEntry             one line: when, what kind, the text
// InitWsDebugLog(dir)    open the external log file, call once from AddonLoad
// ShutdownWsDebugLog()   flush + close the external log file, call once from
//                        AddonUnload, AFTER ShutdownWsClient()
// WsLog / WsLogf         append one line; thread-safe (both ws_client.cpp's
//                        background thread and WinHTTP's callback thread call this)
// GetWsDebugLogSnapshot  thread-safe copy of the in-memory ring buffer, for the
//                        debug window (ws_debug_window.h) to draw
// ClearWsDebugLog        empties the in-memory ring buffer only - the external
//                        file is append-only and unaffected (see below)
// GetWsDebugLogPath      full path of the external log file, for the window's
//                        "open file" affordance
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
// it (parsing, event_id matching, the UI's own read) is at fault. See
// ws_debug_log.cpp for how the log is actually produced.
//--------------------------------------------------------------------------------

#pragma once

#include <cstdint>
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
// wallClockMs   milliseconds since the Unix epoch - what the external log file
//               timestamps each line with
// sessionSec    seconds since this InitWsDebugLog call - what the in-game
//               window shows (a small elapsed counter reads better in a 300px
//               window than a wall-clock timestamp does)
//--------------------------------------------------------------------------------
struct WsLogEntry
{
    int64_t     wallClockMs = 0;
    double      sessionSec  = 0.0;
    WsLogDir    dir         = WsLogDir::Info;
    std::string text;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitWsDebugLog / ShutdownWsDebugLog
//--------------------------------------------------------------------------------
// Call InitWsDebugLog once from AddonLoad, after g_AddonDir is known and before
// InitWsClient - so the very first connect attempt is captured. Call
// ShutdownWsDebugLog once from AddonUnload, after ShutdownWsClient() has returned
// (so the shutdown sequence itself is fully logged before the file is closed).
//--------------------------------------------------------------------------------
void InitWsDebugLog(const std::string& addonDir);
void ShutdownWsDebugLog();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WsLog / WsLogf
//--------------------------------------------------------------------------------
// Appends one line to both the in-memory ring buffer and the external file (and
// mirrors to APIDefs->Log). Safe to call from any thread - internally mutex-
// guarded, since ws_client.cpp's background thread AND WinHTTP's own callback
// thread (StatusCallback, arbitrary WinHTTP-internal thread) both call this.
// Cheap enough to call on every message/state transition; not intended for a per-
// frame render-thread hot path.
//--------------------------------------------------------------------------------
void WsLog(WsLogDir dir, const std::string& text);
void WsLogf(WsLogDir dir, const char* fmt, ...);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetWsDebugLogSnapshot / ClearWsDebugLog / GetWsDebugLogPath
//--------------------------------------------------------------------------------
// GetWsDebugLogSnapshot copies the current ring buffer (oldest-first); cheap
// enough to call every frame the debug window is open, same contract as
// GetRecentReports elsewhere in this addon. ClearWsDebugLog only empties that in-
// memory buffer - the external file is append-only and unaffected, by design (see
// file header). GetWsDebugLogPath returns the external file's full path, valid
// after InitWsDebugLog has been called.
//--------------------------------------------------------------------------------
std::vector<WsLogEntry> GetWsDebugLogSnapshot();
void ClearWsDebugLog();
std::string GetWsDebugLogPath();