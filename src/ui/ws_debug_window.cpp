//################################################################################
// ws_debug_window.cpp   (see: ws_debug_window.h)
//--------------------------------------------------------------------------------

#include "ws_debug_window.h"

#include "imgui.h"
#include "localization.h"
#include "ws_client.h"
#include "ws_debug_log.h"

#include <cstdio>
#include <string>
#include <vector>

bool ShowWsDebugWindow = false;

namespace
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ConnStateLabel   (pairs with: ConnectionStateLabel in live_events_ui.cpp)
    //--------------------------------------------------------------------------------
    // Own copy, not a shared helper - this file has no other dependency on
    // live_events_ui.cpp, and the mapping is one line either way.
    //--------------------------------------------------------------------------------
    const char* ConnStateLabel(WsConnectionState state)
    {
        switch (state)
        {
            case WsConnectionState::Connected:  return Tr("WE_WSDEBUG_CONNECTED");
            case WsConnectionState::Connecting: return Tr("WE_WSDEBUG_CONNECTING");
            default:                            return Tr("WE_WSDEBUG_DISCONNECTED");
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // FormatElapsed
    //--------------------------------------------------------------------------------
    // "mm:ss.mmm" since InitWsDebugLog. An elapsed counter answers "how long after
    // connecting did this happen" at a glance in a narrow window.
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

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
// Registered as its own RT_Render callback in addon.cpp, so it works even outside
// gameplay (loading screens, character select) - useful since a connection
// attempt can happen there too.
//--------------------------------------------------------------------------------
void RenderWsDebugWindow()
{
    if (!ShowWsDebugWindow) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 420.0f), ImGuiCond_FirstUseEver);
    std::string windowLabel = std::string(Tr("WE_WSDEBUG_TITLE")) + kWsDebugWindowId;
    if (!ImGui::Begin(windowLabel.c_str(), &ShowWsDebugWindow))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("%s", Tr("WE_WSDEBUG_SERVER"));
    ImGui::SameLine();
    ImGui::Text("%s", ConnStateLabel(GetConnectionState()));

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    //_ Durable copy is Nexus's own log ("WorldEvents-WS"), not a file - see ws_debug_log.h.
    ImGui::TextDisabled("%s", Tr("WE_WSDEBUG_RESETS_ON_RELOAD"));

    ImGui::Spacing();

    static bool s_autoScroll = true;
    ImGui::Checkbox(Tr("WE_WSDEBUG_AUTOSCROLL"), &s_autoScroll);

    ImGui::SameLine();
    if (ImGui::SmallButton(Tr("WE_WSDEBUG_CLEAR")))
        ClearWsDebugLog();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    std::string filterItems;
    filterItems += Tr("WE_WSDEBUG_FILTER_ALL");   filterItems += '\0';
    filterItems += Tr("WE_WSDEBUG_FILTER_INFO");  filterItems += '\0';
    filterItems += "TX";                          filterItems += '\0';
    filterItems += "RX";                          filterItems += '\0';
    filterItems += Tr("WE_WSDEBUG_FILTER_ERROR"); filterItems += '\0';
    ImGui::Combo("##we_ws_dbg_filter", &s_dirFilter, filterItems.c_str());

    static char s_search[128] = "";
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##we_ws_dbg_search", Tr("WE_WSDEBUG_SEARCH_HINT"), s_search, sizeof(s_search));

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
            ImGui::TableSetupColumn(Tr("WE_WSDEBUG_COL_ELAPSED"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(Tr("WE_WSDEBUG_COL_DIR"),     ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(Tr("WE_WSDEBUG_COL_MESSAGE"), ImGuiTableColumnFlags_WidthStretch);

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