//################################################################################
// ws_debug_window.cpp   (see: ws_debug_window.h)
//--------------------------------------------------------------------------------

#include "ws_debug_window.h"

#include "imgui.h"
#include "ws_client.h"
#include "ws_debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <cstdio>
#include <string>
#include <vector>

bool ShowWsDebugWindow = false;

namespace
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ConnStateLabel   (pairs with: ConnectionStateLabel in live_events_ui.cpp)
    //--------------------------------------------------------------------------------
    // Own copy, not a shared helper - this file has no other dependency on
    // live_events_ui.cpp, and the mapping is one line either way.
    //--------------------------------------------------------------------------------
    const char* ConnStateLabel(WsConnectionState state)
    {
        switch (state)
        {
            case WsConnectionState::Connected:  return "Connected";
            case WsConnectionState::Connecting: return "Connecting...";
            default:                            return "Disconnected";
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // FormatElapsed
    //--------------------------------------------------------------------------------
    // "mm:ss.mmm" since InitWsDebugLog, not a wall-clock timestamp (that's what the
    // external file is for). An elapsed counter answers "how long after connecting
    // did this happen" at a glance in a narrow window.
    //--------------------------------------------------------------------------------
    std::string FormatElapsed(double sessionSec)
    {
        int totalMs = (int)(sessionSec * 1000.0 + 0.5);
        int mm      = totalMs / 60000;
        int ss      = (totalMs / 1000) % 60;
        int ms      = totalMs % 1000;

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d.%03d", mm, ss, ms);
        return buf;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // DirColor / DirTag
    //--------------------------------------------------------------------------------
    // TX cyan, RX green, Error red, Info neutral gray - a color a person can pattern-
    // match down a long scroll without reading every line.
    //--------------------------------------------------------------------------------
    ImVec4 DirColor(WsLogDir dir)
    {
        switch (dir)
        {
            case WsLogDir::Tx:    return ImVec4(0.40f, 0.80f, 1.00f, 1.0f);
            case WsLogDir::Rx:    return ImVec4(0.45f, 0.90f, 0.45f, 1.0f);
            case WsLogDir::Error: return ImVec4(1.00f, 0.45f, 0.45f, 1.0f);
            default:              return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
        }
    }

    const char* DirTag(WsLogDir dir)
    {
        switch (dir)
        {
            case WsLogDir::Tx:    return "TX";
            case WsLogDir::Rx:    return "RX";
            case WsLogDir::Error: return "ERR";
            default:              return "--";
        }
    }

    //_ 0=All, 1=Info, 2=Tx, 3=Rx, 4=Error - index into the combo below.
    int s_dirFilter = 0;

    bool PassesDirFilter(WsLogDir dir)
    {
        switch (s_dirFilter)
        {
            case 1: return dir == WsLogDir::Info;
            case 2: return dir == WsLogDir::Tx;
            case 3: return dir == WsLogDir::Rx;
            case 4: return dir == WsLogDir::Error;
            default: return true;
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderWsDebugWindow
//--------------------------------------------------------------------------------
// See header.
//--------------------------------------------------------------------------------
void RenderWsDebugWindow()
{
    if (!ShowWsDebugWindow) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWsDebugWindowTitle, &ShowWsDebugWindow))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Server:");
    ImGui::SameLine();
    ImGui::Text("%s", ConnStateLabel(GetConnectionState()));

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("Log file:");
    ImGui::SameLine();

    std::string logPath = GetWsDebugLogPath();
    ImGui::TextUnformatted(logPath.empty() ? "(not initialized)" : logPath.c_str());

    if (!logPath.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Open"))
        {
            //_ Opens with whatever app is associated with .log - no need for a second in-app text viewer.
            ShellExecuteA(nullptr, "open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    ImGui::Spacing();

    static bool s_autoScroll = true;
    ImGui::Checkbox("Auto-scroll", &s_autoScroll);

    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        ClearWsDebugLog();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("##we_ws_dbg_filter", &s_dirFilter, "All\0Info\0TX\0RX\0Error\0");

    static char s_search[128] = "";
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##we_ws_dbg_search", "Search text...", s_search, sizeof(s_search));

    ImGui::Separator();

    std::vector<WsLogEntry> entries = GetWsDebugLogSnapshot(); //. oldest-first, see ws_debug_log.h

    //_ Bool-border overload: ImGuiChildFlags_Borders needs ImGui 1.91.1+, and this project's vendored version isn't pinned.
    if (ImGui::BeginChild("##we_ws_dbg_scroll", ImVec2(0.0f, 0.0f), true,
        ImGuiWindowFlags_HorizontalScrollbar))
    {
        //_ BeginTable keeps the elapsed-time column aligned across a scroll that can run to thousands of lines.
        if (ImGui::BeginTable("##we_ws_dbg_table", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("T+",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Dir", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);

            bool hasSearch = s_search[0] != '\0';

            for (const WsLogEntry& e : entries)
            {
                if (!PassesDirFilter(e.dir)) continue;
                if (hasSearch && e.text.find(s_search) == std::string::npos) continue;

                ImGui::TableNextRow();
                ImVec4 col = DirColor(e.dir);

                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", FormatElapsed(e.sessionSec).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(DirTag(e.dir));
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextWrapped("%s", e.text.c_str());
                ImGui::PopStyleColor();
            }

            //_ Standard console-log auto-scroll: stay pinned to bottom if already there before this frame's rows.
            if (s_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}