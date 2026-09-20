//################################################################################
// reset_defaults.cpp
//--------------------------------------------------------------------------------
// See reset_defaults.h for the overall design/rationale.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "addon_options_helpers.h" //. Tooltip
#include "events_categories.h"
#include "events_storage.h"
#include "imgui.h"
#include "localization.h"
#include "reset_defaults.h"

#include <string>

static bool s_open = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawResetToDefaultsButton / DrawResetToDefaultsPopup   (see: reset_defaults.h)
//--------------------------------------------------------------------------------

void DrawResetToDefaultsButton()
{
    //_ Red, matching the "you're about to lose data" tone used for the invalid-API-key state elsewhere in the settings window - not a normal action.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button(Tr("WE_RESET_BUTTON")))
    {
        s_open = true;
        ImGui::OpenPopup(TrId("WE_RESET_POPUP_TITLE", "##popup").c_str());
    }
    ImGui::PopStyleColor(3);

    Tooltip(Tr("WE_TIP_RESET_WIPE"));
}

void DrawResetToDefaultsPopup()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);

    std::string popupId = TrId("WE_RESET_POPUP_TITLE", "##popup");
    if (!ImGui::BeginPopupModal(popupId.c_str(), &s_open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::TextWrapped("%s", Tr("WE_RESET_BODY"));
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Tr("WE_RESET_WARNING"));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(Tr("WE_RESET_CONFIRM"), ImVec2(160, 0)))
    {
        ResetAllDataToDefaults();
        s_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("WE_RESET_CANCEL"), ImVec2(120, 0)))
    {
        s_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawRestoreMissingButton   (see: reset_defaults.h)
//--------------------------------------------------------------------------------
static int s_lastRestoreCount = -1; //. -1 = not clicked yet this session, hides the result text

void DrawRestoreMissingButton()
{
    if (ImGui::Button(Tr("WE_RESTORE_BUTTON")))
        s_lastRestoreCount = RestoreMissingDefaults() + RestoreMissingCategories();

    Tooltip(Tr("WE_TIP_RESTORE"));

    if (s_lastRestoreCount >= 0)
    {
        ImGui::SameLine();
        if (s_lastRestoreCount == 0)
            ImGui::TextDisabled("%s", Tr("WE_RESTORE_RESULT_NONE"));
        else
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), Tr("WE_RESTORE_RESULT_ADDED_FMT"), s_lastRestoreCount);
    }
}