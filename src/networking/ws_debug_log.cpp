//################################################################################
// ws_debug_log.cpp   (see: ws_debug_log.h)
//--------------------------------------------------------------------------------
// Two outputs, written from the same WsLog call so they can never drift apart:
//   1. An in-memory ring buffer (capped, see kMaxEntries below) for the
//      in-game window - instant, no disk I/O, but lost on crash/unload.
//   2. An external file (addonDir\ws_traffic.log), opened once at AddonLoad and
//      appended to (never truncated) for the whole session, flushed after every
//      line - survives a crash, and covers "from the first connection to the
//      last one" the way a bounded in-memory buffer alone can't. Every
//      InitWsDebugLog call writes a banner line marking where a session starts,
//      so multiple play sessions in one file stay easy to tell apart.
// Every line is also mirrored to Nexus's own log via APIDefs->Log at LOGL_TRACE
// under the "WorldEvents-WS" channel, so it's visible in Nexus's existing log
// window too without needing this file's own window open - see AddonOptions for
// where that channel can be raised to LOGL_TRACE if Nexus's log window filters by
// level.
//--------------------------------------------------------------------------------

#include "ws_debug_log.h"

#include "addon.h" //. APIDefs, for the Nexus-log mirror

#include <windows.h> //. GetCurrentThreadId - see WsLog

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace
{
    //_ Capped so an addon left running for days doesn't grow this without bound; the external file (append-only) is the durable record - 5000 lines is generous for the in-game window's own feed.
    constexpr size_t kMaxEntries = 5000;

    std::mutex             s_mutex;
    std::deque<WsLogEntry> s_entries;    //. guarded by s_mutex
    std::ofstream          s_file;       //. guarded by s_mutex
    //_ Set once in InitWsDebugLog; read-only after that.
    std::string            s_filePath;
    std::chrono::steady_clock::time_point s_sessionStart; //. set in InitWsDebugLog

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // DirLabel
    //--------------------------------------------------------------------------------
    // Short fixed-width tag for both the external file and the Nexus-log mirror - the
    // in-game window (ws_debug_window.cpp) draws its own colored label instead of
    // using this one.
    //--------------------------------------------------------------------------------
    const char* DirLabel(WsLogDir dir)
    {
        switch (dir)
        {
        case WsLogDir::Tx:    return "TX";
        case WsLogDir::Rx:    return "RX";
        case WsLogDir::Error: return "ERR";
        default:              return "--";
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // FormatWallClock
    //--------------------------------------------------------------------------------
    // "YYYY-MM-DD HH:MM:SS.mmm", local time - matches what a person tailing the
    // external file alongside e.g. the Cloudflare Worker's own logs wants to eyeball-
    // correlate against.
    //--------------------------------------------------------------------------------
    std::string FormatWallClock(int64_t wallClockMs)
    {
        time_t    secs = (time_t)(wallClockMs / 1000);
        int       ms   = (int)(wallClockMs % 1000);
        std::tm   tmBuf{};
        localtime_s(&tmBuf, &secs);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, ms);
        return buf;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // WriteBanner
    //--------------------------------------------------------------------------------
    // Caller must hold s_mutex. Marks where a session starts/ends in the (potentially
    // multi-session) external file, so "from the first connection to the last one"
    // stays readable even across several play sessions appended to the same file.
    //--------------------------------------------------------------------------------
    void WriteBanner(const std::string& text)
    {
        if (!s_file.is_open()) return;
        s_file << "==== " << text << " ====\n";
        s_file.flush();
    }
}

void InitWsDebugLog(const std::string& addonDir)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    s_sessionStart = std::chrono::steady_clock::now();
    s_entries.clear();

    std::error_code ec;
    //_ addonDir should already exist by this point, but cheap to be sure.
    fs::create_directories(addonDir, ec);

    s_filePath = addonDir + "\\ws_traffic.log";

    //_ Append, not truncate - the file is the durable, multi-session record; only the in-memory ring buffer is session-local (see file header).
    s_file.open(s_filePath, std::ios::out | std::ios::app);

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    WriteBanner("World Events WS debug log opened " + FormatWallClock(nowMs));
}

void ShutdownWsDebugLog()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    WriteBanner("World Events WS debug log closed " + FormatWallClock(nowMs));

    if (s_file.is_open()) s_file.close();
}

void WsLog(WsLogDir dir, const std::string& text)
{
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(s_mutex);

    double sessionSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - s_sessionStart).count();

    s_entries.push_back({nowMs, sessionSec, dir, text});
    while (s_entries.size() > kMaxEntries) s_entries.pop_front();

    if (s_file.is_open())
    {
        s_file << "[" << FormatWallClock(nowMs) << "] [" << DirLabel(dir) << "] "
               << "[tid=" << GetCurrentThreadId() << "] " << text << "\n";
        //_ Flushed one line at a time - crash-safety over throughput (see file header).
        s_file.flush();
    }

    //_ Mirrors into Nexus's own log/window; null-checked as cheap insurance against a crash on the WinHTTP callback thread if APIDefs isn't set yet or has already been cleared.
    if (APIDefs && APIDefs->Log)
    {
        ELogLevel level = (dir == WsLogDir::Error) ? LOGL_WARNING : LOGL_TRACE;
        APIDefs->Log(level, "WorldEvents-WS", text.c_str());
    }
}

void WsLogf(WsLogDir dir, const char* fmt, ...)
{
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    WsLog(dir, std::string(buf));
}

std::vector<WsLogEntry> GetWsDebugLogSnapshot()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return std::vector<WsLogEntry>(s_entries.begin(), s_entries.end());
}

void ClearWsDebugLog()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_entries.clear();
}

std::string GetWsDebugLogPath()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_filePath;
}