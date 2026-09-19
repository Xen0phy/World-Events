//################################################################################
// options_general.cpp   (see: options_general.h)
//--------------------------------------------------------------------------------
// DrawCompetitiveMode      body of the Competitive mode header
// DrawSubscriptionsWindow  body of the Subscriptions window header
// DrawUnsafeZonePreview    yellow outline of the four unsafe-zone edges
// DrawSubscriptionsBar     body of the Subscriptions bar header
// DrawToastPopups         body of the Toast popups header
//--------------------------------------------------------------------------------

#include "options_general.h"

#include "addon_options_helpers.h" //. Tooltip
#include "imgui.h"
#include "localization.h"
#include "maprender.h" //. ScreenFractionToPixels/PixelsToScreenFraction, for the position row
#include "notify_sound.h"
#include "settings.h"
#include "subscriptions_ui.h" //. RequestNotificationLayoutPreview, for the width/position/direction rows

#include <string>
#include <vector>

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
// this window.
// Mirrors the anchor math in subscriptions_bar.cpp: the baseline is the screen
// edge the bar is pinned to, and each side's height drops away from it.
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
// DrawToastPopups
//--------------------------------------------------------------------------------
// Toast popups are a third view of the subscription data, independent of the
// window and the bar. The rows under the enable toggle are disabled while it is
// off. Width, position and stack direction request a layout preview
// (RequestNotificationLayoutPreview) while hovered or active. The sound is one
// .wav file from "<addon dir>/sounds"; which events play it is each row's notify
// level.
//--------------------------------------------------------------------------------
static void DrawToastPopups()
{
    ImGui::Checkbox(Tr("WE_OPT_ENABLE_NOTIFY_POPUPS"), &NotificationsEnabled);
    Tooltip(Tr("WE_TIP_NOTIFY_POPUPS"));

    DisabledBlock(!NotificationsEnabled)
    {
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_WARN_BEFORE_START"), &NotificationLeadMinutes, 0, 0))
        {
            //_ 0 is off
            if (NotificationLeadMinutes < 0)   NotificationLeadMinutes = 0;
            if (NotificationLeadMinutes > 120) NotificationLeadMinutes = 120;
        }
        Tooltip(Tr("WE_TIP_WARN_BEFORE_START"));

        ImGui::Checkbox(Tr("WE_OPT_NOTIFY_ON_START"), &NotificationOnStart);

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_POPUP_DURATION"), &NotificationDisplaySeconds, 0, 0))
        {
            if (NotificationDisplaySeconds < 1)   NotificationDisplaySeconds = 1;
            if (NotificationDisplaySeconds > 120) NotificationDisplaySeconds = 120;
        }
        Tooltip(Tr("WE_TIP_POPUP_DURATION"));

        //_ DragFloat on the underlying float directly - no int round-trip needed since the setting itself is a float.
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragFloat(Tr("WE_OPT_TOAST_WIDTH"), &NotificationPopupWidth, 1, 0, 0, "%.0fpx"))
        {
            if (NotificationPopupWidth < 100.0f) NotificationPopupWidth = 100.0f;
            if (NotificationPopupWidth > 800.0f) NotificationPopupWidth = 800.0f;
        }
        if (ItemPreviewGate()) RequestNotificationLayoutPreview();
        Tooltip(Tr("WE_TIP_TOAST_WIDTH"));

        //_ Shown/edited as pixels, stored as a screen fraction - same convention as DrawFixToScreenRow (addon_options_helpers.cpp).
        {
            ImVec2 anchorPx = ScreenFractionToPixels(NotificationAnchorX, NotificationAnchorY);
            float anchorPos[2] = { anchorPx.x, anchorPx.y };
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat2(Tr("WE_OPT_TOAST_POS"), anchorPos, 1, 0, 0, "%.0fpx"))
            {
                ImVec2 frac = PixelsToScreenFraction({ anchorPos[0], anchorPos[1] });
                NotificationAnchorX = frac.x;
                NotificationAnchorY = frac.y;
            }
            if (ItemPreviewGate()) RequestNotificationLayoutPreview();
        }
        Tooltip(Tr("WE_TIP_TOAST_POS"));

        ImGui::Checkbox(Tr("WE_OPT_TOAST_STACK_UP"), &NotificationStackUpward);
        if (ItemPreviewGate()) RequestNotificationLayoutPreview();
        Tooltip(Tr("WE_TIP_TOAST_STACK_UP"));

        //_ Speaker glyph (notify level 3's icon) drawn in the margin left of the row, marking the combo as the sound.
        {
            float sq = ImGui::GetFrameHeight();
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImVec2 center(rowMin.x - subToggleIndent + sq * 0.5f, rowMin.y + sq * 0.5f);
            DrawSpeakerIcon(ImGui::GetWindowDrawList(), center, sq * 0.96f, ImGui::GetColorU32(ImGuiCol_Text));
        }

        const std::vector<std::string>& soundFiles = GetNotificationSoundFilenames();

        std::vector<const char*> soundLabels;
        soundLabels.push_back(Tr("WE_OPT_SOUND_NONE"));
        for (const auto& fn : soundFiles)
            soundLabels.push_back(fn.c_str());

        int soundIndex = 0; //. "(none)"
        if (!NotificationSoundFile.empty())
            for (int k = 0; k < (int)soundFiles.size(); k++)
                if (soundFiles[k] == NotificationSoundFile) { soundIndex = k + 1; break; }

        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo(Tr("WE_OPT_SOUND"), &soundIndex, soundLabels.data(), (int)soundLabels.size()))
            NotificationSoundFile = (soundIndex == 0) ? std::string() : soundFiles[soundIndex - 1];

        ImGui::SameLine();
        ImGui::TextDisabled("(.wav)");
        ImGui::SameLine();
        if (ImGui::Button(Tr("WE_OPT_RESCAN")))
            ScanNotificationSoundFiles();
        Tooltip(Tr("WE_TIP_RESCAN_SOUNDS"));

        ImGui::SameLine();
        DisabledBlock(NotificationSoundFile.empty())
        {
            if (ImGui::Button(Tr("WE_OPT_TEST")))
                PlayNotificationSound(NotificationSoundFile);
        }
        Tooltip(Tr("WE_TIP_TEST_SOUND"));

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

    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_SUBS_BAR")))
        DrawSubscriptionsBar();

    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_TOAST")))
        DrawToastPopups();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_SECTION_PENDING"));
}