//################################################################################
// notification_client.cpp   (see: notification_client.h)
//--------------------------------------------------------------------------------
// One background thread owns the entire connection lifecycle, structured the same
// way ws_client.cpp's ConnectLoop/StatusCallback/WaitForIoOrCancel are - see that
// file's header for the WinHTTP-async rationale, which applies here unchanged.
// The two differences that matter:
//
//   - This connection sends only a keepalive ping (SendKeepalivePing), not
//     ws_client.cpp's full SendReport - AsyncConn's send fields and
//     WINHTTP_CALLBACK_STATUS_READ_COMPLETE handling are trimmed to that.
//   - The reconnect key is a region string ("EU"/"NA"), and whether a
//     connection is wanted at all is this module's own decision (see
//     RecomputeWantedLocked), not just externally supplied like ws_client's
//     shard key.
//
// FindJsonObjectEnd's poll workaround is reused here too (same WinHTTP build,
// same unreliable completion signal for a receive - see ws_client.cpp) via a
// verbatim copy, not a shared header: the two files' receive loops differ enough
// around it (no history/report-storage step here) that extracting just this one
// function would save less indirection than it costs. Logged through the same
// WsLog/WsLogf ring buffer as ws_client.cpp (see ws_debug_log.h) - one log for a
// user to send beats two.
//--------------------------------------------------------------------------------

#pragma comment(lib, "winhttp.lib")

#include "notification_client.h"

#include "background_threads.h"
#include "events_tracking.h"
#include "gw2_api.h" //. LiveEventsRegion, LiveEventsRegionToWireString - see UpdateNotificationState
#include "notify_host_config.h"
#include <nlohmann/json.hpp>
#include "subscriptions.h"
#include "ws_debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace
{
    constexpr wchar_t kPathPrefix[] = L"/notify?region=";

    //_ Same fixed key as ws_client.cpp's kHostXorKey - see notify_host_config.example.h.
    constexpr unsigned char kNotifyHostXorKey[] = { 0x2D, 0xC6, 0x3C, 0x78, 0x35, 0x9B, 0x3B };

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // DecodeHost
    //--------------------------------------------------------------------------------
    // Reconstructs the notify relay host from notify_host_config.h's kNotifyHostXor
    // bytes - same decode, same only-ever-in-memory-briefly story as ws_client.cpp's
    // DecodeHost, just its own file/constant instead of the shard one.
    //--------------------------------------------------------------------------------
    std::wstring DecodeHost()
    {
        std::string out;
        out.reserve(kNotifyHostXorLen);
        for (size_t i = 0; i < kNotifyHostXorLen; ++i)
            out.push_back(static_cast<char>(kNotifyHostXor[i] ^ kNotifyHostXorKey[i % sizeof(kNotifyHostXorKey)]));
        return std::wstring(out.begin(), out.end());
    }

    constexpr int kBackoffBaseMs = 1000;
    constexpr int kBackoffCapMs  = 30000;
    //_ Sleep granularity, so shutdown/target-change can interrupt promptly.
    constexpr int kBackoffStepMs = 200;

    //_ Backstop bound for the handshake; real cancellation is via s_wakeEvent/s_shutdownEvent.
    constexpr DWORD kHandshakeTimeoutMs = 5000;

    //_ Bounds the best-effort wait for WinHttpWebSocketClose's completion - WinHttpCloseHandle follows regardless.
    constexpr DWORD kCloseTimeoutMs = 2000;

    //_ Ring-buffer cap for DrainLiveEventNotifications - a burst larger than this between two frames drops the oldest, same trim shape as ws_client.cpp's per-event history.
    constexpr size_t kMaxQueuedNotifications = 100;

    //_ Fallback keepalive cadence when nothing arrives to trigger one - see SendKeepalivePing.
    constexpr DWORD kPingFallbackIntervalMs = 45000;

    //_ Backstop for a ping whose completion never arrives - mirrors ws_client.cpp's kSendStaleMs.
    constexpr ULONGLONG kPingStaleMs = 5000;

    //_ Content is ignored server-side (RegionHub.webSocketMessage) - only the send itself matters.
    constexpr char kPingPayload[] = "0";

    //********************************************************************************
    // AsyncConn
    //--------------------------------------------------------------------------------
    // hIoEvent               signaled once per outstanding wait point
    //                        (handshake step, or a WebSocket receive)
    // ioError/ioBytes/       only meaningful immediately after hIoEvent
    //   ioBufferType         signals; read solely by the background thread
    // sendMutex/sendInFlight/ guard the single in-flight keepalive ping issued
    //   sendStartTick/sendBuf by SendKeepalivePing (trimmed ws_client.cpp fields)
    // hSessionHandle         identifies the last handle to close (StatusCallback)
    //--------------------------------------------------------------------------------
    // Per-connection-attempt state, heap-allocated in ConnectOne and freed exactly
    // once, from inside StatusCallback when HANDLE_CLOSING arrives for hSessionHandle
    // - same lifecycle as ws_client.cpp's AsyncConn.
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

    std::mutex s_desiredMutex;
    std::string s_desiredRegionKey; //. guarded by s_desiredMutex; empty = no connection wanted

    //_ Auto-reset "recheck" signal set by UpdateNotificationState/the shutdown hook.
    HANDLE s_wakeEvent = nullptr;

    //_ Manual-reset, set once by the shutdown hook.
    HANDLE s_shutdownEvent = nullptr;

    std::atomic<WsConnectionState> s_connState{WsConnectionState::Disconnected};

    //_ -1 = no presence data yet (fresh connection or disconnected) - see GetRegionViewerCount.
    std::atomic<int> s_regionViewers{-1};

    std::mutex                          s_queueMutex;
    std::deque<LiveEventNotification>   s_queue; //. guarded by s_queueMutex, oldest first

    //_ Cache for RecomputeWantedLocked - avoids rescanning g_SubscribedLiveEvents every UpdateNotificationState call (see notification_client.h).
    std::mutex  s_wantedCacheMutex;
    uint64_t    s_cachedSubGeneration  = 0;
    uint64_t    s_cachedDoneGeneration = 0;
    bool        s_cachedWanted         = false;
    bool        s_cacheValid           = false;

    std::atomic<bool> s_initialized{false};

    //_ Owned by the render/main thread - written once in InitNotificationClient, joined once in ShutdownNotificationClient.
    std::thread s_connectThread;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// StatusCallback
//--------------------------------------------------------------------------------
// Single WINHTTP_STATUS_CALLBACK, registered once per session (ConnectOne) and
// shared via WINHTTP_OPTION_CONTEXT_VALUE across every handle of one connection
// attempt - same shape as ws_client.cpp's, including the send-completion/-error
// branches SendKeepalivePing needs.
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

    //_ Per WINHTTP_WEB_SOCKET_STATUS's own docs, receive completes via WRITE_COMPLETE and send via READ_COMPLETE - swapped relative to their names.
    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE: //. WinHttpWebSocketReceive completed
    {
        auto* status = reinterpret_cast<WINHTTP_WEB_SOCKET_STATUS*>(lpvStatusInformation);
        if (!status)
        {
            WsLog(WsLogDir::Error, "[notify] WRITE_COMPLETE fired with no WINHTTP_WEB_SOCKET_STATUS - treating receive as failed");
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

    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: //. WinHttpWebSocketSend (the keepalive ping) completed
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

            //_ A ping-send error only clears sendInFlight - hIoEvent/ioError/ioBytes belong to the pending receive.
            if (wsResult && wsResult->Operation == WINHTTP_WEB_SOCKET_SEND_OPERATION)
            {
                WsLogf(WsLogDir::Error, "[notify] Keepalive ping send completed with error (err=%lu)",
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
// Shutdown hook (background_threads.h) - see ws_client.cpp's SignalShutdown for
// the full rationale, identical here.
//--------------------------------------------------------------------------------
static void SignalShutdown()
{
    if (s_shutdownEvent) SetEvent(s_shutdownEvent);
    if (s_wakeEvent)     SetEvent(s_wakeEvent);
}

//_ Registered at static-init - simpler/race-free than a lazy first-call registration (same as ws_client.cpp).
static bool s_wakeHookRegistered = ([]
{
    s_wakeEvent     = CreateEventW(nullptr, FALSE, FALSE, nullptr); //. auto-reset
    s_shutdownEvent = CreateEventW(nullptr, TRUE,  FALSE, nullptr); //. manual-reset
    RegisterShutdownHook(SignalShutdown);
    return true;
})();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PushNotificationLocked
//--------------------------------------------------------------------------------
// Appends one notification to s_queue, oldest first, trimming the oldest entry
// once kMaxQueuedNotifications is exceeded. Caller must hold s_queueMutex.
//--------------------------------------------------------------------------------
static void PushNotificationLocked(LiveEventNotification&& incoming)
{
    s_queue.push_back(std::move(incoming));
    while (s_queue.size() > kMaxQueuedNotifications) s_queue.pop_front();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// HandleIncomingMessage
//--------------------------------------------------------------------------------
// Parses one complete text message. A well-formed "report" is queued -
// unfiltered, see notification_client.h for why filtering isn't this module's
// job. A "presence" message just updates s_regionViewers (GetRegionViewerCount)
// in place - nothing to queue, it's replace-in-place standing state, not a one-
// shot event. Anything else (malformed JSON, unknown type, missing fields) is
// silently dropped, same best-effort stance as ws_client.cpp's own version.
//--------------------------------------------------------------------------------
static void HandleIncomingMessage(const std::string& text)
{
    WsLog(WsLogDir::Rx, "[notify] " + text);

    json j;
    try { j = json::parse(text); }
    catch (...)
    {
        WsLogf(WsLogDir::Error, "[notify] Failed to parse incoming message as JSON: %s", text.c_str());
        return;
    }

    std::string type = j.value("type", "");

    if (type == "presence")
    {
        s_regionViewers.store(j.value("region_viewers", -1));
        return;
    }

    if (type != "report")
    {
        WsLogf(WsLogDir::Error, "[notify] Unrecognized message type \"%s\" - ignored", type.c_str());
        return;
    }

    std::string eventId = j.value("event_id", "");
    if (eventId.empty())
    {
        WsLog(WsLogDir::Error, "[notify] \"report\" message missing/empty event_id - dropped");
        return;
    }

    LiveEventNotification note;
    note.eventId       = eventId;
    note.timestampUnix = j.value("ts", (int64_t)0);
    note.reporterName  = j.value("reporter_name", "");
    note.mapId         = j.value("map_id", 0);

    std::lock_guard<std::mutex> lock(s_queueMutex);
    PushNotificationLocked(std::move(note));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShouldExitForTargetChange
//--------------------------------------------------------------------------------
// True if this connection is no longer the one we should be on - same role as
// ws_client.cpp's ShouldExitForShardChange, keyed on region instead of shard.
//--------------------------------------------------------------------------------
static bool ShouldExitForTargetChange(const std::string& connectedKey)
{
    std::lock_guard<std::mutex> lock(s_desiredMutex);
    return s_desiredRegionKey != connectedKey;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitOutcome / WaitForIoOrCancel
//--------------------------------------------------------------------------------
// Identical role/contract to ws_client.cpp's own - see that file for the full
// rationale (not duplicated here).
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
// Verbatim copy of ws_client.cpp's function of the same name - see that file's
// header for why this exists (the poll-workaround story) and the file header
// above for why it's copied, not shared.
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
// SendKeepalivePing
//--------------------------------------------------------------------------------
// Fire-and-forget send of kPingPayload, on this build's WinHTTP the only thing
// observed to flush a receive whose completion callback is otherwise delayed
// indefinitely (see file header). ReceiveLoop calls this right after dispatching
// a message, so the next receive gets reissued within one round-trip instead of
// waiting on kPingFallbackIntervalMs, and again on that fallback timer as a
// backstop. No-op if a previous ping is still in flight and not yet stale by
// kPingStaleMs.
//--------------------------------------------------------------------------------
static void SendKeepalivePing(HINTERNET hWebSocket, AsyncConn& conn)
{
    {
        std::lock_guard<std::mutex> sendLock(conn.sendMutex);
        ULONGLONG nowTick = GetTickCount64();
        if (conn.sendInFlight && (nowTick - conn.sendStartTick) < kPingStaleMs)
            return; //. previous ping still in flight, skip this one

        conn.sendBuf.assign(kPingPayload, kPingPayload + sizeof(kPingPayload) - 1);
        conn.sendInFlight  = true;
        conn.sendStartTick = nowTick;
    }

    DWORD err = WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        conn.sendBuf.data(), (DWORD)conn.sendBuf.size());

    //_ NO_ERROR means queued - completion arrives via StatusCallback's READ_COMPLETE.
    if (err != NO_ERROR)
    {
        WsLogf(WsLogDir::Error, "[notify] Keepalive ping failed to submit (err=%lu)", err);
        std::lock_guard<std::mutex> sendLock(conn.sendMutex);
        conn.sendInFlight = false;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ReceiveLoop
//--------------------------------------------------------------------------------
// Same shape/poll-workaround as ws_client.cpp's ReceiveLoop - see that file's
// header. No history/report-storage step here: every complete message just goes
// straight to HandleIncomingMessage. Fires SendKeepalivePing after each dispatch
// and on a low-frequency fallback timer - see that function for why. Returns true
// if cancelled with a receive still pending (caller must hard-close hWebSocket);
// false if the connection ended on its own.
//--------------------------------------------------------------------------------
static bool ReceiveLoop(HINTERNET hWebSocket, AsyncConn& conn, const std::string& connectedKey)
{
    std::string   messageBuf;
    unsigned char chunk[4096] = {};

    //_ Most recently dispatched message - see ws_client.cpp's ReceiveLoop for why this exists.
    std::string lastDispatched;

    //_ Clock for kPingFallbackIntervalMs - reset by every SendKeepalivePing call below.
    ULONGLONG lastSendTick = GetTickCount64();

    for (;;)
    {
        DWORD bytesReadUnused = 0; //. unused in async mode
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferTypeUnused{};

        DWORD submitErr = WinHttpWebSocketReceive(hWebSocket, chunk, sizeof(chunk), &bytesReadUnused, &bufferTypeUnused);
        if (submitErr != NO_ERROR)
        {
            WsLogf(WsLogDir::Error, "[notify] WinHttpWebSocketReceive failed to submit (region=%s, err=%lu)", connectedKey.c_str(), submitErr);
            return false; //. connection dead, nothing to cancel
        }

        //_ Poll workaround for this build's unreliable completion signal - see ws_client.cpp.
        WaitOutcome outcome;
        for (;;)
        {
            outcome = WaitForIoOrCancel(conn, kReceivePollMs);
            if (outcome != WaitOutcome::TimedOut) break; //. handled below

            size_t polledLen = FindJsonObjectEnd(reinterpret_cast<const char*>(chunk), sizeof(chunk));
            if (polledLen == std::string::npos)
            {
                //_ Nothing complete sitting there yet - still check the fallback keepalive clock.
                if (GetTickCount64() - lastSendTick >= kPingFallbackIntervalMs)
                {
                    SendKeepalivePing(hWebSocket, conn);
                    lastSendTick = GetTickCount64();
                }
                continue;
            }

            std::string candidate(reinterpret_cast<const char*>(chunk), polledLen);
            if (candidate == lastDispatched) continue; //. already dispatched, not new

            HandleIncomingMessage(candidate);
            lastDispatched = candidate;
            SendKeepalivePing(hWebSocket, conn);
            lastSendTick = GetTickCount64();
            //_ The receive issued above is still genuinely pending - keep re-checking, don't reissue it.
        }

        if (outcome == WaitOutcome::Cancelled)
            return true; //. receive pending, caller hard-closes

        if (conn.ioError != 0)
        {
            WsLogf(WsLogDir::Error, "[notify] Receive completed with error (region=%s, err=%lu)", connectedKey.c_str(), conn.ioError);
            return false; //. closed/errored, ConnectLoop reconnects
        }

        //_ ioBufferType can't fully be trusted (see ws_client.cpp) - a real close still surfaces via ioError/teardown as a backup.
        if (conn.ioBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            WsLogf(WsLogDir::Info, "[notify] Server sent a close frame (region=%s)", connectedKey.c_str());
            return false;
        }

        size_t realLen = FindJsonObjectEnd(reinterpret_cast<const char*>(chunk), sizeof(chunk));
        if (realLen == std::string::npos)
        {
            messageBuf.clear();
            continue;
        }

        messageBuf.assign(reinterpret_cast<const char*>(chunk), realLen);

        if (messageBuf == lastDispatched)
        {
            //_ Already dispatched via the poll loop above - this completion just arrived late for the same bytes.
            messageBuf.clear();
            continue;
        }

        HandleIncomingMessage(messageBuf);
        lastDispatched = messageBuf;
        messageBuf.clear();
        SendKeepalivePing(hWebSocket, conn);
        lastSendTick = GetTickCount64();

        //_ Courtesy early-out between messages, so a region switch doesn't wait for the next network event.
        if (ShouldExitForTargetChange(connectedKey)) return false;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitHandshakeStep
//--------------------------------------------------------------------------------
// Identical role/contract to ws_client.cpp's own.
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
// Full asynchronous WinHTTP WebSocket upgrade sequence for one region key - same
// shape as ws_client.cpp's ConnectOne, pointed at kPathPrefix + regionKey against
// the notify host instead of a shard key against the shard host.
//--------------------------------------------------------------------------------
static HINTERNET ConnectOne(const std::string& regionKey, HINTERNET* outConnect, HINTERNET* outSession, AsyncConn** outConn)
{
    *outConnect = nullptr;
    *outSession = nullptr;
    *outConn    = nullptr;

    WsLogf(WsLogDir::Info, "[notify] Connecting (region=%s)...", regionKey.c_str());

    HINTERNET hSession = WinHttpOpen(L"gw2-world-events-notify/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!hSession)
    {
        WsLogf(WsLogDir::Error, "[notify] WinHttpOpen failed (region=%s, GetLastError=%lu)", regionKey.c_str(), GetLastError());
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
        WsLogf(WsLogDir::Error, "[notify] WinHttpConnect failed (region=%s, GetLastError=%lu)", regionKey.c_str(), GetLastError());
        WinHttpCloseHandle(hSession); return nullptr; //. cleanup happens in callback
    }
    WinHttpSetOption(hConnect, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    //_ Region keys are "EU"/"NA" only (LiveEventsRegionToWireString, gw2_api.h) - pure ASCII, safe to widen byte-for-byte.
    std::wstring path = std::wstring(kPathPrefix) + std::wstring(regionKey.begin(), regionKey.end());

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WsLogf(WsLogDir::Error, "[notify] WinHttpOpenRequest failed (region=%s, GetLastError=%lu)", regionKey.c_str(), GetLastError());
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return nullptr;
    }
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    bool upgradeRequested = WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) == TRUE;

    bool handshakeOk = upgradeRequested
        && WaitHandshakeStep(*conn, [&] { return WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                                                      WINHTTP_NO_REQUEST_DATA, 0, 0,
                                                                      reinterpret_cast<DWORD_PTR>(conn)) == TRUE; })
        && WaitHandshakeStep(*conn, [&] { return WinHttpReceiveResponse(hRequest, NULL) == TRUE; });

    HINTERNET hWebSocket = handshakeOk ? WinHttpWebSocketCompleteUpgrade(hRequest, reinterpret_cast<DWORD_PTR>(conn)) : nullptr;

    WinHttpCloseHandle(hRequest);

    if (!hWebSocket)
    {
        WsLogf(WsLogDir::Error, "[notify] WebSocket upgrade failed (region=%s, handshakeOk=%d, ioError=%lu)",
            regionKey.c_str(), handshakeOk ? 1 : 0, conn->ioError);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return nullptr;
    }

    conn->hWebSocketHandle = hWebSocket;
    WinHttpSetOption(hWebSocket, WINHTTP_OPTION_CONTEXT_VALUE, &conn, sizeof(DWORD_PTR));

    WsLogf(WsLogDir::Info, "[notify] Connected (region=%s)", regionKey.c_str());

    *outConnect = hConnect;
    *outSession = hSession;
    return hWebSocket;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CloseGracefully / SleepWithBackoff
//--------------------------------------------------------------------------------
// Identical role/contract to their ws_client.cpp namesakes, targetKey compared
// against s_desiredRegionKey instead of the shard equivalent.
//--------------------------------------------------------------------------------
static void CloseGracefully(HINTERNET hWebSocket, AsyncConn& conn)
{
    DWORD err = WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    if (err == NO_ERROR)
        WaitForIoOrCancel(conn, kCloseTimeoutMs); //. outcome unused, see above
    WinHttpCloseHandle(hWebSocket);
}

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
            if (s_desiredRegionKey != targetKey) return; //. let ConnectLoop react now
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffStepMs));
        slept += kBackoffStepMs;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ConnectLoop
//--------------------------------------------------------------------------------
// Body of the single background thread spawned by InitNotificationClient - same
// shape as ws_client.cpp's ConnectLoop, reconnecting on region change instead of
// shard change, with no reports buffer to clear on switch (nothing here is per-
// target state; s_queue is a plain rolling log of whatever arrived).
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
                desired = s_desiredRegionKey;
            }
            if (IsShuttingDown() || desired != connectedKey) break;
            WaitForSingleObject(s_wakeEvent, INFINITE);
        }
        if (IsShuttingDown()) break;

        if (desired.empty())
        {
            connectedKey.clear();
            s_connState.store(WsConnectionState::Disconnected);
            s_regionViewers.store(-1);
            continue;
        }

        s_connState.store(WsConnectionState::Connecting);

        HINTERNET  hConnect   = nullptr;
        HINTERNET  hSession   = nullptr;
        AsyncConn* conn       = nullptr;
        HINTERNET  hWebSocket = ConnectOne(desired, &hConnect, &hSession, &conn);

        if (!hWebSocket)
        {
            s_connState.store(WsConnectionState::Disconnected);
            ++failCount;
            SleepWithBackoff(failCount, desired);
            continue;
        }

        failCount    = 0;
        connectedKey = desired;
        s_connState.store(WsConnectionState::Connected);

        bool cancelledWithReceivePending = ReceiveLoop(hWebSocket, *conn, connectedKey);

        if (cancelledWithReceivePending)
            WinHttpCloseHandle(hWebSocket); //. receive pending, documented-safe cancel
        else
            CloseGracefully(hWebSocket, *conn); //. ended naturally, safe to close

        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession); //. frees conn via StatusCallback

        WsLogf(WsLogDir::Info, "[notify] Disconnected (region=%s)", connectedKey.c_str());

        s_connState.store(WsConnectionState::Disconnected);
        s_regionViewers.store(-1); //. stale count from connectedKey's region must not leak into the next one
        connectedKey.clear(); //. next iteration reconnects
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RecomputeWantedLocked
//--------------------------------------------------------------------------------
// True if at least one entry in g_SubscribedLiveEvents (subscriptions.h) isn't
// marked done today (events_tracking.h) - the "should a connection be open at
// all" half of UpdateNotificationState's contract (see notification_client.h).
// Cached against GetSubscriptionListGeneration()/GetDoneMarkersGeneration() so a
// call where neither changed is two integer compares, same pattern
// subscriptions_cache.cpp uses for its own rebuild-if-needed check. Caller must
// hold s_wantedCacheMutex.
//--------------------------------------------------------------------------------
static bool RecomputeWantedLocked()
{
    uint64_t subGen  = GetSubscriptionListGeneration();
    uint64_t doneGen = GetDoneMarkersGeneration();

    if (s_cacheValid && subGen == s_cachedSubGeneration && doneGen == s_cachedDoneGeneration)
        return s_cachedWanted;

    bool wanted = false;
    for (const std::string& eventId : g_SubscribedLiveEvents)
    {
        if (!IsLiveEventMarkedDoneToday(eventId)) { wanted = true; break; }
    }

    s_cachedSubGeneration  = subGen;
    s_cachedDoneGeneration = doneGen;
    s_cachedWanted         = wanted;
    s_cacheValid           = true;
    return wanted;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitNotificationClient   (see: notification_client.h)
//--------------------------------------------------------------------------------
void InitNotificationClient()
{
    bool expected = false;
    if (!s_initialized.compare_exchange_strong(expected, true)) return; //. already started

    WsLog(WsLogDir::Info, "[notify] InitNotificationClient: background connection thread starting");
    s_connectThread = std::thread(ConnectLoop);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShutdownNotificationClient   (see: notification_client.h)
//--------------------------------------------------------------------------------
void ShutdownNotificationClient()
{
    WsLog(WsLogDir::Info, "[notify] ShutdownNotificationClient: waiting for background connection thread to exit...");

    //_ Not joinable if InitNotificationClient was never called - std::thread::join() would throw otherwise.
    if (s_connectThread.joinable())
        s_connectThread.join(); //. blocks until ConnectLoop returns

    WsLog(WsLogDir::Info, "[notify] ShutdownNotificationClient: background connection thread has exited");

    //_ Only reached once ConnectLoop has fully returned - safe to close now, or this pair leaks across every DLL reload.
    if (s_wakeEvent)     { CloseHandle(s_wakeEvent);     s_wakeEvent     = nullptr; }
    if (s_shutdownEvent) { CloseHandle(s_shutdownEvent); s_shutdownEvent = nullptr; }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UpdateNotificationState   (see: notification_client.h)
//--------------------------------------------------------------------------------
void UpdateNotificationState(LiveEventsRegion region)
{
    bool wanted;
    {
        std::lock_guard<std::mutex> lock(s_wantedCacheMutex);
        wanted = RecomputeWantedLocked();
    }

    //_ LiveEventsRegionToWireString gives "" for Unknown, so the wanted-but-Unknown case collapses into the same "no connection" key as not-wanted below.
    std::string wireRegion = LiveEventsRegionToWireString(region);
    std::string key        = (wanted && !wireRegion.empty()) ? wireRegion : std::string();

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s_desiredMutex);
        if (s_desiredRegionKey != key)
        {
            s_desiredRegionKey = key;
            changed = true;
        }
    }
    if (!changed) return;

    WsLogf(WsLogDir::Info, "[notify] Target changed -> \"%s\"", key.empty() ? "(none)" : key.c_str());

    //_ Wakes ConnectLoop wherever it's waiting - a target switch is instant either way.
    if (s_wakeEvent) SetEvent(s_wakeEvent);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrainLiveEventNotifications   (see: notification_client.h)
//--------------------------------------------------------------------------------
std::vector<LiveEventNotification> DrainLiveEventNotifications()
{
    std::lock_guard<std::mutex> lock(s_queueMutex);
    std::vector<LiveEventNotification> drained(s_queue.begin(), s_queue.end());
    s_queue.clear();
    return drained;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetNotificationConnectionState   (see: notification_client.h)
//--------------------------------------------------------------------------------
WsConnectionState GetNotificationConnectionState()
{
    return s_connState.load();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetRegionViewerCount   (see: notification_client.h)
//--------------------------------------------------------------------------------
std::optional<int> GetRegionViewerCount()
{
    int v = s_regionViewers.load();
    return v < 0 ? std::nullopt : std::optional<int>(v);
}