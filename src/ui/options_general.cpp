//################################################################################
// options_general.cpp   (see: options_general.h)
//--------------------------------------------------------------------------------
// DrawCompetitiveMode      body of the Competitive mode header
// DrawSubscriptionsWindow  body of the Subscriptions window header
// DrawUnsafeZonePreview    yellow outline of the four unsafe-zone edges
// DrawSubscriptionsBar     body of the Subscriptions bar header
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
// DrawUnsafeZonePreview
//--------------------------------------------------------------------------------
// Draws on the foreground list, so the lines cover the whole screen, not just
// this window. Mirrors the anchor math in subscriptions_bar.cpp: the baseline is
// the screen edge the bar is pinned to, and each side's height drops away from
// it.
//--------------------------------------------------------------------------------
static void DrawUnsafeZonePreview()
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* dl      = ImGui::GetForegroundDrawList();
    const ImU32 yellow  = IM_COL32(255, 220, 0, 255);

    const float dropDir   = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
    const float baselineY = SubscriptionsBarBottomAnchored ? (screen.y - 1.0f) : 1.0f;
    const float heightLeftY  = baselineY + dropDir * (float)SubscriptionsBarUnsafeHeightLeftPx;
    const float heightRightY = baselineY + dropDir * (float)SubscriptionsBarUnsafeHeightRightPx;

    //_ Left zone: vertical edge, then the horizontal top from the screen edge to it.
    dl->AddLine(ImVec2((float)SubscriptionsBarUnsafeLeftPx, baselineY),
                ImVec2((float)SubscriptionsBarUnsafeLeftPx, heightLeftY), yellow, 2.0f);
    dl->AddLine(ImVec2(0.0f, heightLeftY),
                ImVec2((float)SubscriptionsBarUnsafeLeftPx, heightLeftY), yellow, 2.0f);

    //_ Right zone: mirrored, with its own height.
    const float rightX = screen.x - (float)SubscriptionsBarUnsafeRightPx;
    dl->AddLine(ImVec2(rightX, baselineY), ImVec2(rightX, heightRightY), yellow, 2.0f);
    dl->AddLine(ImVec2(rightX, heightRightY), ImVec2(screen.x, heightRightY), yellow, 2.0f);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscriptionsBar
//--------------------------------------------------------------------------------
// The bar is a thin animated line pinned to a screen edge: a second view of the
// subscription data next to the window, with no title bar. The rows under the
// show toggle are disabled while it is off. Each unsafe-zone field shows its
// preview (DrawUnsafeZonePreview) while it is active or hovered.
//--------------------------------------------------------------------------------
static void DrawSubscriptionsBar()
{
    ImGui::Checkbox(Tr("WE_OPT_SHOW_SUBS_BAR"), &ShowSubscriptionsBar);
    DisabledBlock(!ShowSubscriptionsBar)
    {
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);

        ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_ON_BAR"), &SubscriptionsBarHideActive);
        Tooltip(Tr("WE_TIP_HIDE_ACTIVE_ON_BAR"));

        ImGui::Checkbox(Tr("WE_OPT_MINIMAL_MODE"), &SubscriptionsBarMinimalMode);
        ImGui::Checkbox(Tr("WE_OPT_BOTTOM_LINE"),  &SubscriptionsBarBottomAnchored);

        ImGui::ColorEdit4(TrId("WE_OPT_DOT_COLOR", "##bar_dot_color").c_str(), SubscriptionsBarDotColor, kSwatchFlags);

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_POPOUT_HEIGHT"), &SubscriptionsBarMaxDropPx, 0, 0))
        {
            //_ Floored at 8: subscriptions_bar.cpp derives the pill's corner radius from half this value.
            if (SubscriptionsBarMaxDropPx < 8)   SubscriptionsBarMaxDropPx = 8;
            if (SubscriptionsBarMaxDropPx > 300) SubscriptionsBarMaxDropPx = 300;
        }
        Tooltip(Tr("WE_TIP_POPOUT_HEIGHT"));

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_POPOUT_DELAY"), &SubscriptionsBarHoverDelayMs, 0, 0))
        {
            //_ Clamped post-hoc - InputInt allows transient out-of-range input; 0 is valid.
            if (SubscriptionsBarHoverDelayMs < 0)    SubscriptionsBarHoverDelayMs = 0;
            if (SubscriptionsBarHoverDelayMs > 5000) SubscriptionsBarHoverDelayMs = 5000;
        }
        Tooltip(Tr("WE_TIP_POPOUT_DELAY"));

        const float screenWidth  = ImGui::GetIO().DisplaySize.x;
        const float screenHeight = ImGui::GetIO().DisplaySize.y;

        ImGui::Text("%s", Tr("WE_OPT_UNSAFE_ZONE"));
        ImGui::Indent(subToggleIndent);

        //_ Left + right together stay within the screen width; the field not being edited gives way.
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(TrId("WE_OPT_LEFT", "##leftuz").c_str(), &SubscriptionsBarUnsafeLeftPx, 1, 0, 0, "%dpx"))
        {
            if (SubscriptionsBarUnsafeLeftPx < 0)            SubscriptionsBarUnsafeLeftPx = 0;
            if (SubscriptionsBarUnsafeLeftPx > screenWidth)  SubscriptionsBarUnsafeLeftPx = (int)screenWidth;
            if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                SubscriptionsBarUnsafeRightPx = (int)screenWidth - SubscriptionsBarUnsafeLeftPx;
        }
        bool leftActive = ItemPreviewGate();
        Tooltip(Tr("WE_TIP_UNSAFE_LEFT"));

        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(TrId("WE_OPT_RIGHT", "##rightuz").c_str(), &SubscriptionsBarUnsafeRightPx, 1, 0, 0, "%dpx"))
        {
            if (SubscriptionsBarUnsafeRightPx < 0)            SubscriptionsBarUnsafeRightPx = 0;
            if (SubscriptionsBarUnsafeRightPx > screenWidth)  SubscriptionsBarUnsafeRightPx = (int)screenWidth;
            if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                SubscriptionsBarUnsafeLeftPx = (int)screenWidth - SubscriptionsBarUnsafeRightPx;
        }
        bool rightActive = ItemPreviewGate();
        Tooltip(Tr("WE_TIP_UNSAFE_RIGHT"));

        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(TrId("WE_OPT_HEIGHT_LEFT", "##heightuzleft").c_str(), &SubscriptionsBarUnsafeHeightLeftPx, 1, 0, 0, "%dpx"))
        {
            if (SubscriptionsBarUnsafeHeightLeftPx < 0)            SubscriptionsBarUnsafeHeightLeftPx = 0;
            if (SubscriptionsBarUnsafeHeightLeftPx > screenHeight) SubscriptionsBarUnsafeHeightLeftPx = (int)screenHeight;
        }
        bool heightLeftActive = ItemPreviewGate();
        Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_LEFT"));

        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(TrId("WE_OPT_HEIGHT_RIGHT", "##heightuzright").c_str(), &SubscriptionsBarUnsafeHeightRightPx, 1, 0, 0, "%dpx"))
        {
            if (SubscriptionsBarUnsafeHeightRightPx < 0)            SubscriptionsBarUnsafeHeightRightPx = 0;
            if (SubscriptionsBarUnsafeHeightRightPx > screenHeight) SubscriptionsBarUnsafeHeightRightPx = (int)screenHeight;
        }
        bool heightRightActive = ItemPreviewGate();
        Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_RIGHT"));

        ImGui::Unindent(subToggleIndent);
        ImGui::Unindent(subToggleIndent);

        if (leftActive || rightActive || heightLeftActive || heightRightActive)
            DrawUnsafeZonePreview();
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

    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_SUBS_BAR")))
        DrawSubscriptionsBar();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_SECTION_PENDING"));
}