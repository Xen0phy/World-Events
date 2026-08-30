//################################################################################
// ws_debug_log.cpp   (see: ws_debug_log.h)
//--------------------------------------------------------------------------------

#include "ws_debug_log.h"

#include "addon.h" //. APIDefs, for the Nexus-log mirror

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace
{
    //_ Capped so a long session doesn't grow this without bound.
    constexpr size_t kMaxEntries = 5000;

    std::mutex             s_mutex;
    std::deque<WsLogEntry> s_entries; //. guarded by s_mutex
    std::chrono::steady_clock::time_point s_sessionStart; //. set in InitWsDebugLog
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitWsDebugLog / ShutdownWsDebugLog   (see: ws_debug_log.h)
//--------------------------------------------------------------------------------
void InitWsDebugLog()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_sessionStart = std::chrono::steady_clock::now();
    s_entries.clear();
}

void ShutdownWsDebugLog()
{
    //_ No-op - see ws_debug_log.h.
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WsLog / WsLogf   (see: ws_debug_log.h)
//--------------------------------------------------------------------------------
void WsLog(WsLogDir dir, const std::string& text)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    double sessionSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - s_sessionStart).count();

    s_entries.push_back({sessionSec, dir, text});
    while (s_entries.size() > kMaxEntries) s_entries.pop_front();

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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetWsDebugLogSnapshot / ClearWsDebugLog   (see: ws_debug_log.h)
//--------------------------------------------------------------------------------
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