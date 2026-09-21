//################################################################################
// options_help.cpp   (see: options_help.h)
//--------------------------------------------------------------------------------
// kLiveExplainers    heading and body string ids of the three explainer texts
// DrawAbout          version, release date, changelog button
// DrawLiveExplained  the three explainer texts as collapsed headers
// DrawDiagnostics    WS debug window button, ShowDebug render-time metrics
//--------------------------------------------------------------------------------

#include "options_help.h"

#include "addon.h" //. ShowDebug and the g_Avg* render-time averages
#include "build_info.h" //. DateAndTime
#include "changelog_window.h" //. ShowVersionHistoryWindow
#include "imgui.h"
#include "localization.h"
#include "options_widgets.h" //. Tooltip
#include "version.h" //. Maj/Min/Bld/Rev
#include "ws_debug_window.h" //. ShowWsDebugWindow

//_ Heading and body string ids of the three Live Events texts, in display order.
static constexpr const char* kLiveExplainers[][2] = {
    { "WE_OPT_LIVE_HOW_IT_WORKS_HEADING", "WE_OPT_LIVE_HOW_IT_WORKS_BODY" },
    { "WE_OPT_LIVE_WHAT_DATA_HEADING",    "WE_OPT_LIVE_WHAT_DATA_BODY"    },
    { "WE_OPT_LIVE_WHERE_HEADING",        "WE_OPT_LIVE_WHERE_BODY"        },
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawAbout
//--------------------------------------------------------------------------------
// The changelog button opens the window that also opens once after an update
// (changelog_window.h).
//--------------------------------------------------------------------------------
static void DrawAbout()
{
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_HELP_ABOUT"));

    ImGui::Text("%s: %d.%d.%d.%d (%s)", Tr("WE_OPTWIN_HELP_VERSION"), Maj, Min, Bld, Rev, DateAndTime.c_str());

    if (ImGui::Button(Tr("WE_CHANGELOG_TITLE")))
        ShowVersionHistoryWindow = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLiveExplained
//--------------------------------------------------------------------------------
// The prose for the Live tab lives here so that tab stays controls only. Every
// header starts collapsed, like the General tab's.
//--------------------------------------------------------------------------------
static void DrawLiveExplained()
{
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_HELP_LIVE_EXPLAINED"));

    for (const char* const* text : kLiveExplainers)
    {
        if (ImGui::CollapsingHeader(Tr(text[0])))
        {
            ImGui::TextWrapped("%s", Tr(text[1]));
            ImGui::Spacing();
        }
    }
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
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_HELP_DIAGNOSTICS"));

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
// DrawOptionsHelp   (see: options_help.h)
//--------------------------------------------------------------------------------
void DrawOptionsHelp()
{
    DrawAbout();
    ImGui::Spacing();
    DrawLiveExplained();
    ImGui::Spacing();
    DrawDiagnostics();
}