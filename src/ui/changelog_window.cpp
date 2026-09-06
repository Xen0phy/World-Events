//################################################################################
// changelog_window.cpp   (see: changelog_window.h)
//--------------------------------------------------------------------------------
// DrawIndentedNotice   renders one entry's Notes text (file-local)
//--------------------------------------------------------------------------------
// Straight port of Split Wars' helper of the same name/contract (that addon's
// addon.cpp), kept identical so notice text stays interchangeable between the two
// addons.
//--------------------------------------------------------------------------------

#include "changelog_window.h"

#include "imgui.h"
#include "localization.h"
#include "settings.h"
#include "version.h"
#include "version_history.h"

#include <string>

//_ Transient window-visibility flag - see the header comment for why this isn't a SETTING().
bool ShowVersionHistoryWindow = false;

//_ Index into kVersionHistory the dropdown currently shows.
static int s_selectedIndex = 0;

//_ Tracks the closed->open edge across frames so s_selectedIndex resets to newest (0), and SetNextWindowFocus only fires once, on every fresh open - regardless of whether the previous close came from "Got it" or Escape.
static bool s_wasOpenLastFrame = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawIndentedNotice
//--------------------------------------------------------------------------------
// Renders a plain "\n"-separated string (see version_history.h) as headers and
// bullets. Indentation is inferred from leading spaces (2 = one level), since
// ImGui::TextWrapped() doesn't preserve it. An unindented non-bullet line is a
// colored section header; an indented one is plain wrapped text.
//--------------------------------------------------------------------------------
static void DrawIndentedNotice(const char* text)
{
    std::string remaining = text;
    size_t pos = 0;

    while (pos <= remaining.size())
    {
        size_t nl = remaining.find('\n', pos);
        std::string line = (nl == std::string::npos)
            ? remaining.substr(pos)
            : remaining.substr(pos, nl - pos);

        size_t leading = 0;
        while (leading < line.size() && line[leading] == ' ') leading++;
        int indentLevel = (int)(leading / 2);

        std::string content = line.substr(leading);

        if (content.empty())
        {
            ImGui::Spacing();
        }
        else if (content[0] == '*')
        {
            //_ Strip the leading "* " marker - ImGui::Bullet() draws its own.
            std::string text2 = content.substr(content.find_first_not_of("* "));
            for (int i = 0; i < indentLevel; i++) ImGui::Indent();
            ImGui::Bullet();
            ImGui::TextWrapped("%s", text2.c_str());
            for (int i = 0; i < indentLevel; i++) ImGui::Unindent();
        }
        else if (indentLevel == 0)
        {
            //_ An unindented, non-bullet line is a section header ("New Features", "Improvements", ...) - see version_history.h. Colored so it actually reads as a heading instead of blending into the bullet text below it.
            ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.40f, 1.00f), "%s", content.c_str());
        }
        else
        {
            ImGui::TextWrapped("%s", content.c_str());
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CheckForVersionHistoryOnLoad   (see: changelog_window.h)
//--------------------------------------------------------------------------------
// Packs the compiled version the same way Split Wars packs its own currentVersion
// (Maj*1e6 + Min*1e4 + Bld*100 + Rev).
//--------------------------------------------------------------------------------
void CheckForVersionHistoryOnLoad(const std::string& addonDir)
{
    int currentVersion = Maj * 1000000 + Min * 10000 + Bld * 100 + Rev;
    if (LastKnownVersion == currentVersion) return; //. already shown this version's notice

    ShowVersionHistoryWindow = true;
    LastKnownVersion = currentVersion;
    //_ Persisted now, not left for AddonUnload's save - see header for why.
    SaveSettings(addonDir);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderVersionHistoryWindow   (see: changelog_window.h)
//--------------------------------------------------------------------------------
// Registered as its own GUI_Register(RT_Render, ...) callback in AddonLoad, not
// called from AddonRender's IsGameplay-gated block, so the notice is visible at
// character select too - not only once a character is loaded onto a live map
// where a popup stealing input could get someone killed.
//--------------------------------------------------------------------------------
void RenderVersionHistoryWindow()
{
    if (!ShowVersionHistoryWindow)
    {
        s_wasOpenLastFrame = false;
        return;
    }
    if (kVersionHistoryCount == 0) return; //. nothing curated yet

    if (!s_wasOpenLastFrame)
    {
        s_selectedIndex = 0; //. fresh open - default to newest
        //_ Only on the open transition - forcing focus every frame would steal it back from the Combo's own popup, so dropdown clicks never land.
        ImGui::SetNextWindowFocus();
    }
    s_wasOpenLastFrame = true;

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always); //. 0 height = auto-fit
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
               ImGui::GetIO().DisplaySize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin(kVersionHistoryWindowId, nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("WE_CHANGELOG_TITLE"));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (s_selectedIndex >= kVersionHistoryCount) s_selectedIndex = 0; //. stale-index guard
        const VersionHistoryEntry& entry = kVersionHistory[s_selectedIndex];

        std::string comboLabel = std::string("v") + entry.Version;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##VersionHistoryPicker", comboLabel.c_str()))
        {
            for (int i = 0; i < kVersionHistoryCount; i++)
            {
                bool isSelected = (i == s_selectedIndex);
                std::string label = std::string("v") + kVersionHistory[i].Version;
                if (i == 0) label += Tr("WE_CHANGELOG_LATEST_SUFFIX");
                if (ImGui::Selectable(label.c_str(), isSelected))
                    s_selectedIndex = i;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawIndentedNotice(entry.Notes);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btnW = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(Tr("WE_CHANGELOG_GOT_IT"), ImVec2(btnW, 0)))
            ShowVersionHistoryWindow = false;

        ImGui::Spacing();
    }
    ImGui::End();
}