//################################################################################
// options_info.cpp   (see: options_info.h)
//--------------------------------------------------------------------------------
// DrawAbout          version, release date, changelog button
// DrawDiagnostics    WS debug window button, ShowDebug render-time metrics
//--------------------------------------------------------------------------------

#include "options_info.h"

#include "addon.h" //. ShowDebug and the g_Avg* render-time averages
#include "changelog_window.h" //. ShowVersionHistoryWindow
#include "imgui.h"
#include "localization.h"
#include "options_widgets.h" //. Tooltip
#include "version.h" //. Maj/Min/Bld/Rev/DateAndTime
#include "ws_debug_window.h" //. ShowWsDebugWindow

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawAbout
//--------------------------------------------------------------------------------
// The changelog button opens the window that also opens once after an update
// (changelog_window.h).
//--------------------------------------------------------------------------------
static void DrawAbout()
{
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_INFO_ABOUT"));

    ImGui::Text("%s: %d.%d.%d.%d (%s)", Tr("WE_OPTWIN_INFO_VERSION"), Maj, Min, Bld, Rev, DateAndTime.c_str());

    if (ImGui::Button(Tr("WE_CHANGELOG_TITLE")))
        ShowVersionHistoryWindow = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDiagnostics
//--------------------------------------------------------------------------------
// The button only sets ShowWsDebugWindow; RenderWsDebugWindow (ws_debug_window.h)
// draws the window. The metric lines are compiled in only while ShowDebug
// (addon.h) is true and are English-only.
//--------------------------------------------------------------------------------
static void DrawDiagnostics()
{
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_INFO_DIAGNOSTICS"));

    if (ImGui::Button(Tr("WE_OPT_LIVE_DEBUG_WS_BUTTON")))
        ShowWsDebugWindow = true;
    Tooltip(Tr("WE_OPT_LIVE_DEBUG_WS_TIP"));

    if constexpr (ShowDebug)
    {
        ImGui::Spacing();

        //_ "Render" = AddonRender's own cost (rings/bar/window/notify); "Options UI" = this settings window's, while it is open.
        ImGui::TextDisabled("Render: %.3f ms avg (1s)", g_AvgRenderTimeMs);
        ImGui::TextDisabled("Options UI: %.3f ms avg (1s)", g_AvgOptionsRenderTimeMs);

        //_ Per-view Data (cache refresh) vs Draw (pixel work) split, so "why is view X slow" maps to one number.
        ImGui::TextDisabled("Bar: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsBarDataMs, g_AvgSubsBarDrawMs);
        ImGui::TextDisabled("Window: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsWindowDataMs, g_AvgSubsWindowDrawMs);
        ImGui::TextDisabled("Notify: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsNotifyDataMs, g_AvgSubsNotifyDrawMs);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsInfo   (see: options_info.h)
//--------------------------------------------------------------------------------
void DrawOptionsInfo()
{
    DrawAbout();
    ImGui::Spacing();
    DrawDiagnostics();
}