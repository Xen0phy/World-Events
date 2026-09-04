//################################################################################
// ws_client.cpp   (see: ws_client.h)
//--------------------------------------------------------------------------------
// One background thread owns the entire connection lifecycle: connect, receive
// loop, disconnect, backoff, reconnect. UpdateShard/SendReport, called from the
// render thread, only touch mutex-guarded shared variables and never block on I/O
// themselves.
//
// Built on WinHTTP's asynchronous mode (WINHTTP_FLAG_ASYNC) - the only mode where
// cancelling a pending WebSocket receive from another thread is documented as
// safe. Every step runs through WinHttpSetStatusCallback completions (see
// StatusCallback, WaitForIoOrCancel) instead of blocking calls. AsyncConn holds
// per-connection-attempt state, heap-allocated in ConnectOne and freed exactly
// once from inside StatusCallback, when HANDLE_CLOSING arrives for the attempt's
// session handle.
//
// This is the addon's one long-lived background thread, so it's joined by name
// (ShutdownWsClient, ws_client.h), not detached like the short- lived ones
// elsewhere. hConnect/hSession/hWebSocket/AsyncConn for the current attempt live
// as locals inside ConnectLoop; s_hWebSocket/s_activeConn mirror them under
// s_handleMutex so SendReport (render thread) has something to send on.
//--------------------------------------------------------------------------------

#pragma comment(lib, "winhttp.lib")

#include "ws_client.h"

#include "background_threads.h"
#include "gw2_api.h" //. GetLiveEventsRegion/LiveEventsRegionToWireString, for the outgoing "region" field
#include "host_config.h"
#include <nlohmann/json.hpp>
#include "ws_debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace
{
    constexpr wchar_t kPathPrefix[] = L"/ws?shard=";

    //_ Must match KEY in tools/generate_host_config.py exactly, byte for byte.
    constexpr unsigned char kHostXorKey[] = { 0x2D, 0xC6, 0x3C, 0x78, 0x35, 0x9B, 0x3B };

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // DecodeHost
    //--------------------------------------------------------------------------------
    // Reconstructs the real relay host from host_config.h's obfuscated bytes, widened
    // to what WinHttpConnect expects. XOR-obfuscated, not encrypted, just keeps the
    // host out of a strings/hex-editor pass over the built DLL; see
    // host_config.example.h for the fuller picture. Only ever called right before the
    // connect that needs it - the plain host exists in memory only as long as it
    // takes WinHTTP to consume it.
    //--------------------------------------------------------------------------------
    std::wstring DecodeHost()
    {
        std::string out;
        out.reserve(kHostXorLen);
        for (size_t i = 0; i < kHostXorLen; ++i)
            out.push_back(static_cast<char>(kHostXor[i] ^ kHostXorKey[i % sizeof(kHostXorKey)]));
        return std::wstring(out.begin(), out.end());
    }

    constexpr int kBackoffBaseMs = 1000;
    constexpr int kBackoffCapMs  = 30000;
    //_ Sleep granularity, so shutdown/shard-change can interrupt promptly.
    constexpr int kBackoffStepMs = 200;

    //_ Backstop bound for the handshake; real cancellation is via s_wakeEvent/s_shutdownEvent (see WaitForIoOrCancel).
    constexpr DWORD kHandshakeTimeoutMs = 5000;

    //_ Bounds the best-effort wait for WinHttpWebSocketClose's completion (CloseGracefully) - WinHttpCloseHandle follows regardless.
    constexpr DWORD kCloseTimeoutMs = 2000;

    //_ Backstop for a send whose completion never arrives (see FindJsonObjectEnd) - well above the ~200ms worst-case real round trip observed via the WS debug log.
    constexpr ULONGLONG kSendStaleMs = 5000;

    //********************************************************************************
    // AsyncConn
    //--------------------------------------------------------------------------------
    // hIoEvent               signaled once per outstanding wait point
    //                        (handshake step, or a WebSocket receive)
    // ioError/ioBytes/       only meaningful immediately after hIoEvent
    //   ioBufferType         signals; read solely by the background thread
    // sendMutex/sendInFlight/ guard the single in-flight WinHttpWebSocketSend
    //   sendStartTick/sendBuf issued by SendReport, from the render thread
    // hSessionHandle         identifies the last handle to close (StatusCallback)
    // hWebSocketHandle       set once known, for future log/assert use
    //--------------------------------------------------------------------------------
    // Per-connection-attempt state, heap-allocated in ConnectOne and freed exactly
    // once - see file header - from inside StatusCallback when HANDLE_CLOSING arrives
    // for hSessionHandle. Shared as the WinHTTP context value
    // (WINHTTP_OPTION_CONTEXT_VALUE) across all four handles of one attempt
    // (session/connect/request/websocket), set explicitly on each, not relied upon to
    // be inherited.
    //--------------------------------------------------------------------------------
    struct AsyncConn
    {
        HANDLE hIoEvent = nullptr; //. auto-reset

        DWORD                          ioError      = 0; //. 0 = success
        DWORD                          ioBytes       = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE ioBufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

        std::mutex        sendMutex;
        bool              sendInFlight  = false; //. guarded by sendMutex
        ULONGLONG         sendStartTick = 0;      //. guarded by sendMutex, GetTickCount64() at send start
        std::vector<char> sendBuf;                //. guarded by sendMutex

        HINTERNET hSessionHandle   = nullptr;
        HINTERNET hWebSocketHandle = nullptr;

        AsyncConn()  { hIoEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
        ~AsyncConn() { if (hIoEvent) CloseHandle(hIoEvent); }

        AsyncConn(const AsyncConn&)            = delete;
        AsyncConn& operator=(const AsyncConn&) = delete;
    };

    std::mutex s_handleMutex;
    HINTERNET  s_hWebSocket = nullptr; //. guarded by s_handleMutex
    AsyncConn* s_activeConn = nullptr; //. guarded by s_handleMutex

    std::mutex  s_desiredMutex;
    std::string s_desiredShardKey; //. guarded by s_desiredMutex

    //_ Auto-reset "recheck" signal set by UpdateShard/the shutdown hook, consumed by whichever wait the background thread is in.
    HANDLE s_wakeEvent = nullptr;

    //_ Manual-reset, set once by the shutdown hook - distinct from s_wakeEvent since WaitForMultipleObjects needs a HANDLE, not a poll.
    HANDLE s_shutdownEvent = nullptr;

    std::atomic<WsConnectionState> s_connState{WsConnectionState::Disconnected};

    std::mutex                                                s_reportsMutex;
    std::unordered_map<std::string, std::deque<EventReport>>  s_reports; //. guarded by s_reportsMutex

    std::atomic<bool> s_initialized{false};

    //_ Owned by the render/main thread - written once in InitWsClient, joined once in ShutdownWsClient (not detached).
    std::thread s_connectThread;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// StatusCallback
//--------------------------------------------------------------------------------
// Single WINHTTP_STATUS_CALLBACK, registered once per session (ConnectOne) and
// shared via WINHTTP_OPTION_CONTEXT_VALUE across every handle of one connection
// attempt. May run on an arbitrary WinHTTP worker thread; dwContext is the
// AsyncConn* for this attempt (null very early, before it's set on a handle -
// defensively ignored). Per WINHTTP_WEB_SOCKET_STATUS's own docs,
// WinHttpWebSocketReceive completes via WRITE_COMPLETE and WinHttpWebSocketSend
// via READ_COMPLETE - the two are swapped relative to their names.
//--------------------------------------------------------------------------------
static void CALLBACK StatusCallback(HINTERNET hInternet, DWORD_PTR dwContext,
    DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD /*dwStatusInformationLength*/)
{
    AsyncConn* conn = reinterpret_cast<AsyncConn*>(dwContext);
    if (!conn) return;

    switch (dwInternetStatus)
    {
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        //_ hSessionHandle closes last of this attempt's four handles - safe to free conn only here.
        if (hInternet == conn->hSessionHandle)
            //_ Must be the last line in this file that touches conn.
            delete conn;
        return;

    //_ Pre-upgrade handshake success only; errors for these same calls arrive via REQUEST_ERROR below.
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        conn->ioError = 0;
        SetEvent(conn->hIoEvent);
        return;

    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE: //. WinHttpWebSocketReceive completed
    {
        auto* status = reinterpret_cast<WINHTTP_WEB_SOCKET_STATUS*>(lpvStatusInformation);
        if (!status)
        {
            //_ No status struct - treat as a failed receive, not a silently truncated one.
            WsLog(WsLogDir::Error, "WRITE_COMPLETE fired with no WINHTTP_WEB_SOCKET_STATUS - treating receive as failed");
            conn->ioError = ERROR_INTERNAL_ERROR;
            SetEvent(conn->hIoEvent);
            return;
        }
        conn->ioError      = 0;
        conn->ioBytes      = status->dwBytesTransferred;
        conn->ioBufferType = status->eBufferType;
        SetEvent(conn->hIoEvent);
        return;
    }

    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: //. WinHttpWebSocketSend completed
    {
        std::lock_guard<std::mutex> lock(conn->sendMutex);
        conn->sendInFlight = false; //. fire-and-forget, unobserved
        return;
    }

    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
    {
        //_ Branch on handle identity first - pre-upgrade errors carry WINHTTP_ASYNC_RESULT, post-upgrade carry WINHTTP_WEB_SOCKET_ASYNC_RESULT.
        if (hInternet == conn->hWebSocketHandle)
        {
            auto* wsResult = reinterpret_cast<WINHTTP_WEB_SOCKET_ASYNC_RESULT*>(lpvStatusInformation);

            //_ A send error only clears sendInFlight - hIoEvent/ioError/ioBytes belong to whichever receive WaitForIoOrCancel awaits.
            if (wsResult && wsResult->Operation == WINHTTP_WEB_SOCKET_SEND_OPERATION)
            {
                WsLogf(WsLogDir::Error, "WinHttpWebSocketSend completed with error (err=%lu)",
                    wsResult->AsyncResult.dwError);
                std::lock_guard<std::mutex> lock(conn->sendMutex);
                conn->sendInFlight = false;
                return;
            }

            conn->ioError = (wsResult && wsResult->AsyncResult.dwError) ? wsResult->AsyncResult.dwError : ERROR_INTERNAL_ERROR;
            SetEvent(conn->hIoEvent);
            return;
        }

        auto* result = reinterpret_cast<WINHTTP_ASYNC_RESULT*>(lpvStatusInformation);
        conn->ioError = (result && result->dwError) ? result->dwError : ERROR_INTERNAL_ERROR;
        SetEvent(conn->hIoEvent);
        return;
    }

    default:
        return; //. not relevant here
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SignalShutdown
//--------------------------------------------------------------------------------
// Shutdown hook (background_threads.h), runs on whatever thread calls
// WaitForBackgroundThreads (normally the render thread during AddonUnload).
// Touches no WinHTTP handle at all - just sets two Win32 events so the background
// thread, wherever it's waiting (idling in ConnectLoop or inside
// WaitForIoOrCancel), wakes immediately and reacts to IsShuttingDown() itself.
//--------------------------------------------------------------------------------
static void SignalShutdown()
{
    if (s_shutdownEvent) SetEvent(s_shutdownEvent);
    if (s_wakeEvent)     SetEvent(s_wakeEvent);
}

//_ Registered at static-init - simpler/race-free than a lazy first-call registration (same as gw2_api.cpp).
static bool s_wakeHookRegistered = ([]
{
    s_wakeEvent     = CreateEventW(nullptr, FALSE, FALSE, nullptr); //. auto-reset
    s_shutdownEvent = CreateEventW(nullptr, TRUE,  FALSE, nullptr); //. manual-reset
    RegisterShutdownHook(SignalShutdown);
    return true;
})();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ParseEventReportFields
//--------------------------------------------------------------------------------
// Pulls ts/reporter_name/region out of one report-shaped JSON object (a top-level
// "report" message, or one entry of a "history" message's "reports" array - same
// field set either way, see file header). reporter_name/region default to empty
// instead of failing the whole entry, since a server that hasn't been upgraded to
// send them yet (live-toast-handoff.md section 8) still sends a valid event_id/ts
// otherwise.
//--------------------------------------------------------------------------------
static EventReport ParseEventReportFields(const std::string& eventId, const json& entry)
{
    EventReport report;
    report.eventId       = eventId;
    report.timestampUnix = entry.value("ts", (int64_t)0);
    report.reporterName  = entry.value("reporter_name", "");
    report.region        = entry.value("region", "");
    return report;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PushReportsLocked
//--------------------------------------------------------------------------------
// Inserts `incoming` into s_reports[eventId], then sorts newest-first and trims
// to 10. Sorting instead of assuming send order means a "history" message doesn't
// have to trust the server sent entries in any particular order. Caller must hold
// s_reportsMutex.
//--------------------------------------------------------------------------------
static void PushReportsLocked(const std::string& eventId, const EventReport& incoming)
{
    auto& dq = s_reports[eventId];
    dq.push_back(incoming);
    std::sort(dq.begin(), dq.end(), [](const EventReport& a, const EventReport& b)
    {
        return a.timestampUnix > b.timestampUnix;
    });
    while (dq.size() > 10) dq.pop_back();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// HandleIncomingMessage
//--------------------------------------------------------------------------------
// Parses one complete text message and applies it to s_reports. Unknown/
// malformed messages are silently ignored - a live-events feed on best effort
// shouldn't take the connection down over one bad frame.
//--------------------------------------------------------------------------------
static void HandleIncomingMessage(const std::string& text)
{
    //_ Raw wire text, logged before parsing - makes "did the message arrive" a fact instead of a guess in the UI.
    WsLog(WsLogDir::Rx, text);

    json j;
    try { j = json::parse(text); }
    catch (...)
    {
        WsLogf(WsLogDir::Error, "Failed to parse incoming message as JSON: %s", text.c_str());
        return;
    }

    std::string type = j.value("type", "");

    if (type == "report")
    {
        std::string eventId = j.value("event_id", "");
        if (eventId.empty())
        {
            WsLog(WsLogDir::Error, "\"report\" message missing/empty event_id - dropped");
            return;
        }

        std::lock_guard<std::mutex> lock(s_reportsMutex);
        PushReportsLocked(eventId, ParseEventReportFields(eventId, j));
    }
    else if (type == "history")
    {
        if (!j.contains("reports") || !j["reports"].is_array())
        {
            WsLog(WsLogDir::Error, "\"history\" message missing/malformed \"reports\" array - dropped");
            return;
        }

        std::lock_guard<std::mutex> lock(s_reportsMutex);
        for (const auto& entry : j["reports"])
        {
            std::string eventId = entry.value("event_id", "");
            if (eventId.empty()) continue;
            PushReportsLocked(eventId, ParseEventReportFields(eventId, entry));
        }
    }
    else
    {
        WsLogf(WsLogDir::Error, "Unrecognized message type \"%s\" - ignored", type.c_str());
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShouldExitForShardChange
//--------------------------------------------------------------------------------
// True if this connection is no longer the one we should be on: UpdateShard
// pointed us somewhere else (shutdown is checked separately by the caller via the
// WaitForIoOrCancel outcome, not this). connectedKey is the shard this call is
// servicing (thread-confined, passed down from ConnectLoop).
//--------------------------------------------------------------------------------
static bool ShouldExitForShardChange(const std::string& connectedKey)
{
    std::lock_guard<std::mutex> lock(s_desiredMutex);
    return s_desiredShardKey != connectedKey;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitOutcome / WaitForIoOrCancel
//--------------------------------------------------------------------------------
// The one wait primitive every blocking step in this file goes through: waits for
// conn's completion event, or either shared cancellation event, or boundMs -
// whichever comes first. The background thread is never actually blocked inside a
// WinHTTP call while this runs (the call already returned, async- pending), so
// reacting to Cancelled here by closing the handle ourselves is the pattern the
// WinHTTP docs describe as safe - no cross-thread handle access required.
//--------------------------------------------------------------------------------
enum class WaitOutcome { Completed, Cancelled, TimedOut };

static WaitOutcome WaitForIoOrCancel(AsyncConn& conn, DWORD boundMs)
{
    HANDLE handles[3] = { conn.hIoEvent, s_wakeEvent, s_shutdownEvent };
    DWORD result = WaitForMultipleObjects(3, handles, FALSE, boundMs);

    if (result == WAIT_OBJECT_0)     return WaitOutcome::Completed;
    if (result == WAIT_OBJECT_0 + 1) return WaitOutcome::Cancelled; //. s_wakeEvent, caller re-checks
    if (result == WAIT_OBJECT_0 + 2) return WaitOutcome::Cancelled; //. s_shutdownEvent
    return WaitOutcome::TimedOut;                                   //. or WAIT_FAILED, same handling
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FindJsonObjectEnd
//--------------------------------------------------------------------------------
// conn.ioBytes/ioBufferType are not trustworthy on this WinHTTP build: they
// consistently report SendReport()'s most recent send (67 bytes, UTF8_MESSAGE)
// instead of the actual receive, because the concurrent send/receive on one
// handle cross-contaminate their WINHTTP_WEB_SOCKET_STATUS (confirmed against
// docs and observed Rx byte counts via the WS debug log). `chunk` itself is not
// corrupted, though, and reliably holds the real message at offset 0 - so this
// finds the message's end directly from its content (brace depth, string/escape
// state) instead of trusting either field. Returns npos if no complete top-level
// object is found within len bytes.
//--------------------------------------------------------------------------------
static size_t FindJsonObjectEnd(const char* data, size_t len)
{
    int  depth    = 0;
    bool started  = false;
    bool inString = false;
    bool escaped  = false;

    for (size_t i = 0; i < len; ++i)
    {
        char c = data[i];

        if (inString)
        {
            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  inString = false;
            continue;
        }

        switch (c)
        {
        case '"':
            inString = true;
            break;
        case '{':
            depth++;
            started = true;
            break;
        case '}':
            depth--;
            if (started && depth == 0)
                return i + 1; //. closes the top-level object
            break;
        default:
            break;
        }
    }
    return std::string::npos; //. no complete object found
}

//_ Bound for the inner poll wait below - a local re-check interval, not a network round-trip timeout.
constexpr DWORD kReceivePollMs = 200;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ReceiveLoop
//--------------------------------------------------------------------------------
// Issues WinHttpWebSocketReceive (async) and waits on WaitForIoOrCancel - no
// bound, since UpdateShard/shutdown wake it directly via s_wakeEvent/
// s_shutdownEvent. Also polls kReceivePollMs at a time and re-checks `chunk`
// directly (see FindJsonObjectEnd): on this WinHTTP build the completion
// notification doesn't reliably fire when data physically arrives, so the poll is
// what actually surfaces new messages promptly; lastDispatched stops the eventual
// (delayed) real completion from dispatching the same bytes twice. Returns true
// if cancelled with a receive still pending (caller must hard-close hWebSocket);
// false if the connection ended on its own.
//--------------------------------------------------------------------------------
static bool ReceiveLoop(HINTERNET hWebSocket, AsyncConn& conn, const std::string& connectedKey)
{
    std::string   messageBuf;
    unsigned char chunk[4096] = {};

    //_ Most recently dispatched message - see header above for why this exists.
    std::string lastDispatched;

    for (;;)
    {
        DWORD bytesReadUnused = 0; //. unused in async mode
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferTypeUnused{};

        WsLogf(WsLogDir::Info, "Issuing WinHttpWebSocketReceive (shard=%s, bufferedSoFar=%zu bytes)",
            connectedKey.c_str(), messageBuf.size());

        DWORD submitErr = WinHttpWebSocketReceive(hWebSocket, chunk, sizeof(chunk), &bytesReadUnused, &bufferTypeUnused);
        if (submitErr != NO_ERROR)
        {
            WsLogf(WsLogDir::Error, "WinHttpWebSocketReceive failed to submit (shard=%s, err=%lu)", connectedKey.c_str(), submitErr);
            return false; //. connection dead, nothing to cancel
        }

        //_ Poll workaround for this build's unreliable completion signal - see header above.
        WaitOutcome outcome;
        for (;;)
        {
            outcome = WaitForIoOrCancel(conn, kReceivePollMs);
            if (outcome != WaitOutcome::TimedOut) break; //. handled below

            size_t polledLen = FindJsonObjectEnd(reinterpret_cast<const char*>(chunk), sizeof(chunk));
            if (polledLen == std::string::npos) continue; //. nothing complete sitting there yet

            std::string candidate(reinterpret_cast<const char*>(chunk), polledLen);
            if (candidate == lastDispatched) continue; //. already dispatched, not new

            WsLogf(WsLogDir::Info, "New data found in buffer via poll, dispatching (shard=%s, realLen=%zu bytes)",
                connectedKey.c_str(), candidate.size());
            HandleIncomingMessage(candidate);
            lastDispatched = candidate;
            //_ The receive issued above is still genuinely pending - keep re-checking, don't reissue it.
        }

        if (outcome == WaitOutcome::Cancelled)
        {
            WsLogf(WsLogDir::Info, "Receive cancelled (shard/shutdown) with a receive still pending (shard=%s)", connectedKey.c_str());
            return true; //. receive pending, caller hard-closes
        }

        //_ Completed: conn.ioError/ioBytes/ioBufferType now hold this receive's result.
        if (conn.ioError != 0)
        {
            WsLogf(WsLogDir::Error, "Receive completed with error (shard=%s, err=%lu)", connectedKey.c_str(), conn.ioError);
            return false; //. closed/errored, ConnectLoop reconnects
        }

        WsLogf(WsLogDir::Info, "Receive completed (shard=%s, reported ioBytes=%lu, reported bufferType=%d - both unreliable, see FindJsonObjectEnd)",
            connectedKey.c_str(), conn.ioBytes, (int)conn.ioBufferType);

        //_ ioBufferType can't be trusted for the CLOSE case either - a real close still surfaces via ioError/teardown as a backup.
        if (conn.ioBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            WsLogf(WsLogDir::Info, "Server sent a close frame (shard=%s)", connectedKey.c_str());
            return false;
        }

        size_t realLen = FindJsonObjectEnd(reinterpret_cast<const char*>(chunk), sizeof(chunk));
        if (realLen == std::string::npos)
        {
            WsLogf(WsLogDir::Error,
                "No complete top-level JSON object found in receive buffer (shard=%s, reported ioBytes=%lu) - dropping",
                connectedKey.c_str(), conn.ioBytes);
            messageBuf.clear();
            continue;
        }

        messageBuf.assign(reinterpret_cast<const char*>(chunk), realLen);

        if (messageBuf == lastDispatched)
        {
            //_ Already dispatched via the poll loop above - this completion just arrived late for the same bytes.
            WsLogf(WsLogDir::Info, "Receive completed for already-dispatched data (shard=%s, realLen=%zu bytes) - skipping",
                connectedKey.c_str(), messageBuf.size());
            messageBuf.clear();
            continue;
        }

        WsLogf(WsLogDir::Info, "Message complete, dispatching (shard=%s, realLen=%zu bytes, reported ioBytes=%lu)",
            connectedKey.c_str(), messageBuf.size(), conn.ioBytes);

        HandleIncomingMessage(messageBuf);
        lastDispatched = messageBuf;
        messageBuf.clear();

        //_ Courtesy early-out between messages, so a shard switch doesn't wait for the next network event.
        if (ShouldExitForShardChange(connectedKey)) return false;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitHandshakeStep
//--------------------------------------------------------------------------------
// One phase of the pre-upgrade handshake: call `submit` (expected to issue an
// async WinHTTP call and return its immediate result), then wait for its
// completion, bounded by kHandshakeTimeoutMs as a backstop (see that constant's
// comment) and cancellable via WaitForIoOrCancel like everything else. Returns
// false on submit failure, cancellation, timeout, or a reported error -
// ConnectOne treats all of those identically (this attempt failed, clean up and
// let ConnectLoop back off/retry).
//--------------------------------------------------------------------------------
template <typename SubmitFn>
static bool WaitHandshakeStep(AsyncConn& conn, SubmitFn submit)
{
    if (!submit()) return false;

    WaitOutcome outcome = WaitForIoOrCancel(conn, kHandshakeTimeoutMs);
    if (outcome != WaitOutcome::Completed) return false; //. cancelled or timed out
    return conn.ioError == 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ConnectOne
//--------------------------------------------------------------------------------
// Full asynchronous WinHTTP WebSocket upgrade sequence for one shard key. On
// success, *outConnect/*outSession are left open (the WebSocket handle depends on
// them for its whole lifetime) and *outConn is the AsyncConn this connection's
// completions arrive on - the caller passes it to ReceiveLoop and stores it in
// s_activeConn. Returns nullptr on any failure; *outConn is still set on failure
// paths past the point hSession exists, since its `delete` happens entirely
// inside StatusCallback (see file header), not here.
//--------------------------------------------------------------------------------
static HINTERNET ConnectOne(const std::string& shardKey, HINTERNET* outConnect, HINTERNET* outSession, AsyncConn** outConn)
{
    *outConnect = nullptr;
    *outSession = nullptr;
    *outConn    = nullptr;

    WsLogf(WsLogDir::Info, "Connecting (shard=%s)...", shardKey.c_str());

    HINTERNET hSession = WinHttpOpen(L"gw2-world-events-ws/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!hSession)
    {
        WsLogf(WsLogDir::Error, "WinHttpOpen failed (shard=%s, GetLastError=%lu)", shardKey.c_str(), GetLastError());
        return nullptr; //_ No handle exists yet - nothing for a callback to ever fire on.
    }

    AsyncConn* conn = new AsyncConn();
    conn->hSessionHandle = hSession;
    *outConn = conn;

    WinHttpSetStatusCallback(hSession, StatusCallback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));
    WinHttpSetTimeouts(hSession, kHandshakeTimeoutMs, kHandshakeTimeoutMs, kHandshakeTimeoutMs, kHandshakeTimeoutMs);

    std::wstring host = DecodeHost();
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WsLogf(WsLogDir::Error, "WinHttpConnect failed (shard=%s, GetLastError=%lu)", shardKey.c_str(), GetLastError());
        WinHttpCloseHandle(hSession); return nullptr; //. cleanup happens in callback
    }
    WinHttpSetOption(hConnect, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    //_ Shard keys are our own "map%u-%016llx" hex format - pure ASCII, safe to widen byte-for-byte.
    std::wstring path = std::wstring(kPathPrefix) + std::wstring(shardKey.begin(), shardKey.end());

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WsLogf(WsLogDir::Error, "WinHttpOpenRequest failed (shard=%s, GetLastError=%lu)", shardKey.c_str(), GetLastError());
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return nullptr;
    }
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    bool upgradeRequested = WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) == TRUE;

    //_ TRUE means queued (expect a success callback or REQUEST_ERROR); FALSE failed synchronously, no callback will ever come.
    bool handshakeOk = upgradeRequested
        && WaitHandshakeStep(*conn, [&] { return WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                                                      WINHTTP_NO_REQUEST_DATA, 0, 0,
                                                                      reinterpret_cast<DWORD_PTR>(conn)) == TRUE; })
        && WaitHandshakeStep(*conn, [&] { return WinHttpReceiveResponse(hRequest, NULL) == TRUE; });

    HINTERNET hWebSocket = handshakeOk ? WinHttpWebSocketCompleteUpgrade(hRequest, reinterpret_cast<DWORD_PTR>(conn)) : nullptr;

    //_ Safe to close now - the upgraded WebSocket handle no longer needs it; if cancelled mid-handshake, this is the same documented-safe async cancel.
    WinHttpCloseHandle(hRequest);

    if (!hWebSocket)
    {
        WsLogf(WsLogDir::Error, "WebSocket upgrade failed (shard=%s, handshakeOk=%d, ioError=%lu)",
            shardKey.c_str(), handshakeOk ? 1 : 0, conn->ioError);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return nullptr;
    }

    conn->hWebSocketHandle = hWebSocket;
    WinHttpSetOption(hWebSocket, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    WsLogf(WsLogDir::Info, "Connected (shard=%s)", shardKey.c_str());

    *outConnect = hConnect;
    *outSession = hSession;
    return hWebSocket;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CloseGracefully
//--------------------------------------------------------------------------------
// Best-effort WebSocket close handshake before the hard WinHttpCloseHandle, so
// the server sees an intentional disconnect instead of the connection just
// vanishing. Only called when ReceiveLoop returned because the connection ended
// on its own (no receive left pending) - see ConnectLoop. Bounded and
// unconditional: whatever WaitForIoOrCancel returns, WinHttpCloseHandle follows
// regardless (see kCloseTimeoutMs).
//--------------------------------------------------------------------------------
static void CloseGracefully(HINTERNET hWebSocket, AsyncConn& conn)
{
    DWORD err = WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    if (err == NO_ERROR)
        WaitForIoOrCancel(conn, kCloseTimeoutMs); //. outcome unused, see above
    WinHttpCloseHandle(hWebSocket);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SleepWithBackoff
//--------------------------------------------------------------------------------
// Capped exponential backoff between (re)connect attempts, slept in small
// increments so shutdown or a shard change breaks out early instead of riding out
// the full delay. Also used for the plain "nothing to do yet" wait, with
// failCount 0 (i.e. the base delay), since that needs the same interruptible-
// sleep shape.
//--------------------------------------------------------------------------------
static void SleepWithBackoff(int failCount, const std::string& targetKey)
{
    //_ 1000 * 2^8 already exceeds the cap below.
    int shift   = (std::min)(failCount, 8);
    int delayMs = (std::min)(kBackoffBaseMs * (1 << shift), kBackoffCapMs);

    int slept = 0;
    while (slept < delayMs)
    {
        if (IsShuttingDown()) return;

        {
            std::lock_guard<std::mutex> lock(s_desiredMutex);
            if (s_desiredShardKey != targetKey) return; //. let ConnectLoop react now
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffStepMs));
        slept += kBackoffStepMs;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ConnectLoop
//--------------------------------------------------------------------------------
// Body of the single background thread spawned by InitWsClient. Waits for a
// desired shard key, connects, runs ReceiveLoop until the connection ends for any
// reason, cleans up, and repeats - reconnecting to the same shard (transient
// drop) or a new one (UpdateShard changed the target), with backoff on repeated
// failures.
//--------------------------------------------------------------------------------
static void ConnectLoop()
{
    BackgroundThreadGuard guard;

    std::string connectedKey; //. thread-confined, no lock needed
    int         failCount = 0;

    while (!IsShuttingDown())
    {
        std::string desired;
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(s_desiredMutex);
                desired = s_desiredShardKey;
            }
            if (IsShuttingDown() || desired != connectedKey) break;
            WaitForSingleObject(s_wakeEvent, INFINITE);
        }
        if (IsShuttingDown()) break;

        if (desired.empty())
        {
            //_ Not currently connected by construction - nothing to close; explicit branch/no-op for clarity.
            connectedKey.clear();
            s_connState.store(WsConnectionState::Disconnected);
            continue;
        }

        s_connState.store(WsConnectionState::Connecting);
        {
            std::lock_guard<std::mutex> lock(s_reportsMutex);
            s_reports.clear(); //. stale data from prior shard
        }

        HINTERNET  hConnect   = nullptr;
        HINTERNET  hSession   = nullptr;
        AsyncConn* conn       = nullptr;
        HINTERNET  hWebSocket = ConnectOne(desired, &hConnect, &hSession, &conn);

        if (!hWebSocket)
        {
            s_connState.store(WsConnectionState::Disconnected);
            ++failCount;
            WsLogf(WsLogDir::Info, "Connect attempt failed (shard=%s, failCount=%d) - backing off before retry", desired.c_str(), failCount);
            SleepWithBackoff(failCount, desired);
            continue;
        }

        failCount    = 0;
        connectedKey = desired;
        {
            std::lock_guard<std::mutex> lock(s_handleMutex);
            s_hWebSocket = hWebSocket;
            s_activeConn = conn;
        }
        s_connState.store(WsConnectionState::Connected);

        bool cancelledWithReceivePending = ReceiveLoop(hWebSocket, *conn, connectedKey);

        {
            std::lock_guard<std::mutex> lock(s_handleMutex);
            s_hWebSocket = nullptr;
            s_activeConn = nullptr;
        }

        if (cancelledWithReceivePending)
            WinHttpCloseHandle(hWebSocket); //. receive pending, documented-safe cancel
        else
            CloseGracefully(hWebSocket, *conn); //. ended naturally, safe to close

        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession); //. frees conn via StatusCallback

        WsLogf(WsLogDir::Info, "Disconnected (shard=%s)", connectedKey.c_str());

        s_connState.store(WsConnectionState::Disconnected);
        connectedKey.clear(); //. next iteration reconnects
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitWsClient   (see: ws_client.h)
//--------------------------------------------------------------------------------
void InitWsClient()
{
    bool expected = false;
    if (!s_initialized.compare_exchange_strong(expected, true)) return; //. already started

    WsLog(WsLogDir::Info, "InitWsClient: background connection thread starting");
    s_connectThread = std::thread(ConnectLoop);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShutdownWsClient   (see: ws_client.h)
//--------------------------------------------------------------------------------
// The one long-lived background thread in the addon - every other one
// (gw2_api.cpp, subscriptions.cpp) is short-lived and fine to just poll via
// WaitForBackgroundThreads. This thread spends most of its life waiting on a
// pending WinHttpWebSocketReceive; the unbounded join here is only safe because
// that wait (and every other wait this thread can be in) is now interruptible on
// demand via s_wakeEvent/s_shutdownEvent - see file header.
//--------------------------------------------------------------------------------
void ShutdownWsClient()
{
    WsLog(WsLogDir::Info, "ShutdownWsClient: waiting for background connection thread to exit...");

    //_ Not joinable if InitWsClient was never called - std::thread::join() would throw otherwise.
    if (s_connectThread.joinable())
        s_connectThread.join(); //. blocks until ConnectLoop returns

    WsLog(WsLogDir::Info, "ShutdownWsClient: background connection thread has exited");

    //_ Only reached once ConnectLoop has fully returned - safe to close now, or this pair leaks across every DLL reload.
    if (s_wakeEvent)     { CloseHandle(s_wakeEvent);     s_wakeEvent     = nullptr; }
    if (s_shutdownEvent) { CloseHandle(s_shutdownEvent); s_shutdownEvent = nullptr; }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UpdateShard   (see: ws_client.h)
//--------------------------------------------------------------------------------
void UpdateShard(const ShardIdentity& shard)
{
    std::string key = shard.valid ? shard.ToKey() : std::string();

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s_desiredMutex);
        if (s_desiredShardKey != key)
        {
            s_desiredShardKey = key;
            changed = true;
        }
    }
    if (!changed) return;

    WsLogf(WsLogDir::Info, "Shard changed -> \"%s\"", key.empty() ? "(none)" : key.c_str());

    //_ Wakes ConnectLoop wherever it's waiting (idle, or inside WaitForIoOrCancel) - a shard switch is instant either way.
    if (s_wakeEvent) SetEvent(s_wakeEvent);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SendReport   (see: ws_client.h)
//--------------------------------------------------------------------------------
void SendReport(const std::string& eventId, const std::string& reporterName)
{
    json j;
    j["type"]          = "report";
    j["event_id"]      = eventId;
    j["reporter_name"] = reporterName;
    j["region"]        = LiveEventsRegionToWireString(GetLiveEventsRegion()); //. computed fresh at send time - see ws_client.h
    std::string payload = j.dump();

    std::lock_guard<std::mutex> lock(s_handleMutex); //. held across the send call
    if (!s_hWebSocket || !s_activeConn)
    {
        WsLogf(WsLogDir::Error, "SendReport(%s) dropped - not connected", eventId.c_str());
        return; //. not connected
    }

    AsyncConn* conn = s_activeConn;
    {
        std::lock_guard<std::mutex> sendLock(conn->sendMutex);
        ULONGLONG nowTick = GetTickCount64();
        if (conn->sendInFlight && (nowTick - conn->sendStartTick) < kSendStaleMs)
        {
            WsLogf(WsLogDir::Error, "SendReport(%s) dropped - previous send still in flight", eventId.c_str());
            return; //. drop under backpressure
        }
        if (conn->sendInFlight)
        {
            WsLogf(WsLogDir::Info, "SendReport(%s) - previous send's completion never arrived, treating as stale after %llums",
                eventId.c_str(), (unsigned long long)(nowTick - conn->sendStartTick));
        }

        conn->sendBuf.assign(payload.begin(), payload.end());
        conn->sendInFlight  = true;
        conn->sendStartTick = nowTick;
    }

    WsLog(WsLogDir::Tx, payload);

    DWORD err = WinHttpWebSocketSend(s_hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        conn->sendBuf.data(), (DWORD)conn->sendBuf.size());

    //_ NO_ERROR means queued - completion arrives via StatusCallback's READ_COMPLETE, same contract as ReceiveLoop's WRITE_COMPLETE.
    if (err != NO_ERROR)
    {
        WsLogf(WsLogDir::Error, "WinHttpWebSocketSend failed to submit (event_id=%s, err=%lu)", eventId.c_str(), err);
        //_ Failed to even submit - no completion will ever clear sendInFlight for this call.
        std::lock_guard<std::mutex> sendLock(conn->sendMutex);
        conn->sendInFlight = false;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetRecentReports   (see: ws_client.h)
//--------------------------------------------------------------------------------
std::vector<EventReport> GetRecentReports(const std::string& eventId)
{
    std::lock_guard<std::mutex> lock(s_reportsMutex);
    auto it = s_reports.find(eventId);
    if (it == s_reports.end()) return {};
    return std::vector<EventReport>(it->second.begin(), it->second.end()); //. already newest-first, see PushReportsLocked
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetConnectionState   (see: ws_client.h)
//--------------------------------------------------------------------------------
WsConnectionState GetConnectionState()
{
    return s_connState.load();
}