//################################################################################
// options_general.cpp   (see: options_general.h)
//--------------------------------------------------------------------------------
// DrawCompetitiveMode      body of the Competitive mode header
// DrawSubscriptionsWindow  body of the Subscriptions window header
//--------------------------------------------------------------------------------

#include "options_general.h"

#include "addon_options_helpers.h" //. Tooltip
#include "imgui.h"
#include "localization.h"
#include "settings.h"

//_ Swatch button only, no numeric fields; the click opens a hue-wheel picker.
static constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCompetitiveMode
//--------------------------------------------------------------------------------
// The master checkbox is a derived AND of the three per-view settings, not a
// setting of its own: toggling it writes all three. The indented checkboxes below
// it each set one view (AddonRender reads them, see addon.cpp).
//--------------------------------------------------------------------------------
static void DrawCompetitiveMode()
{
    bool disableAll = DisableWindowWhenCompetitive && DisableBarWhenCompetitive && DisableNotifyWhenCompetitive;
    if (ImGui::Checkbox(Tr("WE_OPT_DISABLE_COMPETITIVE"), &disableAll))
    {
        DisableWindowWhenCompetitive = disableAll;
        DisableBarWhenCompetitive    = disableAll;
        DisableNotifyWhenCompetitive = disableAll;
    }
    Tooltip(Tr("WE_TIP_DISABLE_COMPETITIVE"));

    float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::Indent(subToggleIndent);
    ImGui::Checkbox(TrId("WE_OPT_WINDOW", "##dis_comp_window").c_str(), &DisableWindowWhenCompetitive);
    ImGui::Checkbox(TrId("WE_OPT_BAR",    "##dis_comp_bar").c_str(),    &DisableBarWhenCompetitive);
    ImGui::Checkbox(TrId("WE_OPT_TOAST",  "##dis_comp_toast").c_str(),  &DisableNotifyWhenCompetitive);
    ImGui::Unindent(subToggleIndent);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscriptionsWindow
//--------------------------------------------------------------------------------
// The show toggle only opens or closes the window; which events are subscribed
// lives in events.json. The rows under it are disabled while the window is off.
//--------------------------------------------------------------------------------
static void DrawSubscriptionsWindow()
{
    ImGui::Checkbox(Tr("WE_OPT_SHOW_SUBS_WINDOW"), &ShowSubscriptionsWindow);
    DisabledBlock(!ShowSubscriptionsWindow)
    {
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);
        ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_IN_WINDOW"), &SubscriptionsHideActive);

        //_ RGB only (feeds TextColored), not a tinted dot/icon like BasicEventColor*, which need alpha.
        ImGui::ColorEdit3(TrId("WE_OPT_ACTIVE", "##sub_color_active").c_str(), SubscriptionsActiveColor, kSwatchFlags);
        ImGui::ColorEdit3(TrId("WE_OPT_SOON",   "##sub_color_soon").c_str(),   SubscriptionsSoonColor,   kSwatchFlags);
        ImGui::Unindent(subToggleIndent);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsGeneral   (see: options_general.h)
//--------------------------------------------------------------------------------
void DrawOptionsGeneral()
{
    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_COMPETITIVE")))
        DrawCompetitiveMode();

    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_SUBS_WINDOW")))
        DrawSubscriptionsWindow();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_SECTION_PENDING"));
}