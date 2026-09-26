//################################################################################
// options_general.cpp   (see: options_general.h)
//--------------------------------------------------------------------------------
// ItemPreviewGate          widget just drawn is active or hovered
// BuildChatChannelOptions  entries of the Paste to combo
// DrawCompetitiveMode      body of the Competitive mode header
// DrawSubscriptionsWindow  body of the Subscriptions window header
// DrawUnsafeZonePreview    yellow outline of the four unsafe-zone edges
// DrawSubscriptionsBar     body of the Subscriptions bar header
// DrawToastPopups          body of the Toast popups header
// DrawChatAndPaste         body of the Chat and paste header
// DrawAccountAndTracking   body of the Account and tracking header
//--------------------------------------------------------------------------------

#include "options_general.h"

#include "addon.h"
#include "better_chat.h" //. IsBetterChatLoaded/IsBetterChatSelfCommandEnabled, for the status line and combo
#include "gw2_api.h" //. GetGw2ApiStatus, for the key status word
#include "imgui.h"
#include "localization.h"
#include "maprender.h" //. ScreenFractionToPixels/PixelsToScreenFraction, for the position row
#include "notify_sound.h"
#include "options_widgets.h" //. Tooltip, DisabledBlock, SubToggleIndent/SubToggleIndentWidth, kSwatchFlags, kWarningColor, DrawSpeakerIcon, DrawFileCombo
#include "settings.h"
#include "subscriptions_ui.h" //. RequestNotificationLayoutPreview, for the width/position/direction rows

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ItemPreviewGate
//--------------------------------------------------------------------------------
// True while the widget just drawn is being dragged or typed into (IsItemActive)
// or moused over (IsItemHovered), the condition that shows its live preview. The
// unsafe-zone lines (DrawUnsafeZonePreview) and the toast layout preview
// (RequestNotificationLayoutPreview, subscriptions_ui.h) gate on it. Call right
// after the widget it applies to.
//--------------------------------------------------------------------------------
static bool ItemPreviewGate()
{
    return ImGui::IsItemActive() || ImGui::IsItemHovered();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BuildChatChannelOptions
//--------------------------------------------------------------------------------
// Fills labels/prefixes with the "Paste to" combo's entries, in matching index
// order. Index 0 is the empty prefix (ChatChannelPrefix's "current chat"
// default); the Better Chat entry stays last so dropping it is a single tail
// check. It is dropped unless IsBetterChatSelfCommandEnabled() (better_chat.h) is
// true: offering it otherwise would let the player pick a channel that just types
// "/self ..." into whatever chat box has focus.
//--------------------------------------------------------------------------------
static void BuildChatChannelOptions(std::vector<const char*>& labels, std::vector<const char*>& prefixes)
{
    static const char* const kLabels[] = {
        "Current chat (default)", "Say", "Party", "Squad",
        "Guild (represented)", "Guild 1", "Guild 2", "Guild 3",
        "Guild 4", "Guild 5", "Map", "Whisper (/w self)",
        "Better Chat (/self)"
    };
    static const char* const kPrefixes[] = {
        "", "/s ", "/p ", "/d ",
        "/g ", "/g1 ", "/g2 ", "/g3 ",
        "/g4 ", "/g5 ", "/m ", "/w ",
        "/self "
    };
    constexpr int kCount = sizeof(kLabels) / sizeof(kLabels[0]);

    for (int i = 0; i < kCount; i++)
    {
        if (std::string(kPrefixes[i]) == "/self " && !IsBetterChatSelfCommandEnabled()) continue;
        labels.push_back(kLabels[i]);
        prefixes.push_back(kPrefixes[i]);
    }
}

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

    SubToggleIndent indent;
    ImGui::Checkbox(TrId("WE_OPT_WINDOW", "##dis_comp_window").c_str(), &DisableWindowWhenCompetitive);
    ImGui::Checkbox(TrId("WE_OPT_BAR",    "##dis_comp_bar").c_str(),    &DisableBarWhenCompetitive);
    ImGui::Checkbox(TrId("WE_OPT_TOAST",  "##dis_comp_toast").c_str(),  &DisableNotifyWhenCompetitive);
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
        SubToggleIndent indent;
        ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_IN_WINDOW"), &SubscriptionsHideActive);

        //_ RGB only (feeds TextColored), not a tinted dot/icon like BasicEventColor*, which need alpha.
        ImGui::ColorEdit3(TrId("WE_OPT_ACTIVE", "##sub_color_active").c_str(), SubscriptionsActiveColor, kSwatchFlags);
        ImGui::ColorEdit3(TrId("WE_OPT_SOON",   "##sub_color_soon").c_str(),   SubscriptionsSoonColor,   kSwatchFlags);
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
        SubToggleIndent indent;
        
        //_ half size of the current ImGUI window - to be used with GroupBox
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        float startX = ImGui::GetCursorPosX();

        ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_ON_BAR"), &SubscriptionsBarHideActive);
        Tooltip(Tr("WE_TIP_HIDE_ACTIVE_ON_BAR"));

        ImGui::Checkbox(Tr("WE_OPT_MINIMAL_MODE"), &SubscriptionsBarMinimalMode);
        ImGui::Checkbox(Tr("WE_OPT_BOTTOM_LINE"),  &SubscriptionsBarBottomAnchored);

        ImGui::ColorEdit4(TrId("WE_OPT_DOT_COLOR", "##bar_dot_color").c_str(), SubscriptionsBarDotColor, kSwatchFlags);
        
        SeparatorText(Tr("WE_OPT_POPOUT"));
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(Tr("WE_OPT_HEIGHT"), &SubscriptionsBarMaxDropPx, 1, 8,300,"%dpx"))
        {
            //_ Floored at 8: subscriptions_bar.cpp derives the pill's corner radius from half this value.
            SubscriptionsBarMaxDropPx = std::clamp(SubscriptionsBarMaxDropPx, 8, 300);
        }
        Tooltip(Tr("WE_TIP_POPOUT_HEIGHT"));
        ImGui::SameLine(startX + half + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragInt(Tr("WE_OPT_DELAY"), &SubscriptionsBarHoverDelayMs, 1, 0, 5000, "%dms"))
        {
            //_ Clamped post-hoc - InputInt allows transient out-of-range input; 0 is valid.
            SubscriptionsBarHoverDelayMs = std::clamp(SubscriptionsBarHoverDelayMs, 0, 5000);
        }
        Tooltip(Tr("WE_TIP_POPOUT_DELAY"));

        const float screenWidth  = ImGui::GetIO().DisplaySize.x;
        const float screenHeight = ImGui::GetIO().DisplaySize.y;

        SeparatorText(Tr("WE_OPT_UNSAFE_ZONE"));
        GroupBox(Tr("WE_OPT_LEFT"), half)
        {
            //_ Left + right together stay within the screen width; the field not being edited gives way.
            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt(TrId("WE_OPT_WIDTH", "##leftuz").c_str(), &SubscriptionsBarUnsafeLeftPx, 1, 0, 0, "%dpx"))
            {
                SubscriptionsBarUnsafeLeftPx = std::clamp(SubscriptionsBarUnsafeLeftPx, 0, (int)screenWidth);
                if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                    SubscriptionsBarUnsafeRightPx = (int)screenWidth - SubscriptionsBarUnsafeLeftPx;
            }
            bool leftActive = ItemPreviewGate();
            Tooltip(Tr("WE_TIP_UNSAFE_LEFT"));

            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt(TrId("WE_OPT_HEIGHT", "##heightuzleft").c_str(), &SubscriptionsBarUnsafeHeightLeftPx, 1, 0, 0, "%dpx"))
            {
                SubscriptionsBarUnsafeHeightLeftPx = std::clamp(SubscriptionsBarUnsafeHeightLeftPx, 0, (int)screenHeight);
            }
            bool heightLeftActive = ItemPreviewGate();
            Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_LEFT"));

            if ((leftActive || heightLeftActive) && NexusLink && NexusLink->IsGameplay)
                DrawUnsafeZonePreview();
        }

        ImGui::SameLine();

        GroupBox(Tr("WE_OPT_RIGHT"), half)
        {
            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt(TrId("WE_OPT_WIDTH", "##rightuz").c_str(), &SubscriptionsBarUnsafeRightPx, 1, 0, 0, "%dpx"))
            {
                SubscriptionsBarUnsafeRightPx = std::clamp(SubscriptionsBarUnsafeRightPx, 0, (int)screenWidth);
                if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                    SubscriptionsBarUnsafeLeftPx = (int)screenWidth - SubscriptionsBarUnsafeRightPx;
            }
            bool rightActive = ItemPreviewGate();
            Tooltip(Tr("WE_TIP_UNSAFE_RIGHT"));

            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt(TrId("WE_OPT_HEIGHT", "##heightuzright").c_str(), &SubscriptionsBarUnsafeHeightRightPx, 1, 0, 0, "%dpx"))
            {
                SubscriptionsBarUnsafeHeightRightPx = std::clamp(SubscriptionsBarUnsafeHeightRightPx, 0, (int)screenHeight);
            }
            bool heightRightActive = ItemPreviewGate();
            Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_RIGHT"));

            if ((rightActive || heightRightActive) && NexusLink && NexusLink->IsGameplay)
                DrawUnsafeZonePreview();
        }
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
        SubToggleIndent indent;

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_WARN_BEFORE_START"), &NotificationLeadMinutes, 0, 0))
        {
            //_ 0 is off
            NotificationLeadMinutes = std::clamp(NotificationLeadMinutes, 0, 120);
        }
        Tooltip(Tr("WE_TIP_WARN_BEFORE_START"));

        ImGui::Checkbox(Tr("WE_OPT_NOTIFY_ON_START"), &NotificationOnStart);

        ImGui::SetNextItemWidth(50);
        if (ImGui::InputInt(Tr("WE_OPT_POPUP_DURATION"), &NotificationDisplaySeconds, 0, 0))
        {
            NotificationDisplaySeconds = std::clamp(NotificationDisplaySeconds, 1, 120);
        }
        Tooltip(Tr("WE_TIP_POPUP_DURATION"));

        //_ DragFloat on the underlying float directly - no int round-trip needed since the setting itself is a float.
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragFloat(Tr("WE_OPT_TOAST_WIDTH"), &NotificationPopupWidth, 1, 0, 0, "%.0fpx"))
        {
            NotificationPopupWidth = std::clamp(NotificationPopupWidth, 100.0f, 800.0f);
        }
        if (ItemPreviewGate()) RequestNotificationLayoutPreview();
        Tooltip(Tr("WE_TIP_TOAST_WIDTH"));

        //_ Shown/edited as pixels, stored as a screen fraction - same convention as DrawFixToScreenRow (options_events_rows.cpp).
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
            ImVec2 center(rowMin.x - SubToggleIndentWidth() + sq * 0.5f, rowMin.y + sq * 0.5f);
            DrawSpeakerIcon(ImGui::GetWindowDrawList(), center, sq * 0.96f, ImGui::GetColorU32(ImGuiCol_Text));
        }

        DrawFileCombo(Tr("WE_OPT_SOUND"), "WE_OPT_SOUND_NONE", GetNotificationSoundFilenames(), NotificationSoundFile);

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
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawChatAndPaste
//--------------------------------------------------------------------------------
// Everything here feeds PasteToChat (subscriptions.cpp), shared by all three
// subscription views. The status line under the channel combo reports Better
// Chat's /self, which the combo offers only while it is usable.
//--------------------------------------------------------------------------------
static void DrawChatAndPaste()
{
    //_ Session-only unlock; the delay field stays greyed until ticked.
    static bool unlockDelay = false;
    ImGui::Checkbox("##lock_delay", &unlockDelay);
    Tooltip(Tr("WE_TIP_UNLOCK_PASTE_DELAY"));
    ImGui::SameLine();
    DisabledBlock(!unlockDelay)
    {
        ImGui::SetNextItemWidth(50.0f);
        if (ImGui::InputInt(Tr("WE_OPT_PASTE_DELAY"), &delayMilliseconds, 0, 0))
        {
            //_ Clamped post-hoc - InputInt allows transient out-of-range input; 0 is valid.
            delayMilliseconds = std::clamp(delayMilliseconds, 0, 100);
        }
    }

    std::vector<const char*> chatChannelLabels;
    std::vector<const char*> chatChannelPrefixes;
    BuildChatChannelOptions(chatChannelLabels, chatChannelPrefixes);

    int chatChannelIndex = 0;
    for (int ci = 0; ci < (int)chatChannelPrefixes.size(); ci++)
    {
        if (ChatChannelPrefix == chatChannelPrefixes[ci]) { chatChannelIndex = ci; break; }
    }

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo(Tr("WE_OPT_PASTE_TO"), &chatChannelIndex, chatChannelLabels.data(), (int)chatChannelLabels.size()))
        ChatChannelPrefix = chatChannelPrefixes[chatChannelIndex];
    Tooltip(Tr("WE_TIP_PASTE_TO"));

    if (!IsBetterChatLoaded())                  ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_NOT_LOADED"));
    else if (!IsBetterChatSelfCommandEnabled()) ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_SELF_DISABLED"));
    else                                        ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_SELF_ENABLED"));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawAccountAndTracking
//--------------------------------------------------------------------------------
// The only place the GW2 API key is edited.
//--------------------------------------------------------------------------------
static void DrawAccountAndTracking()
{
    //_ Not gated by window/bar/notifications visibility: drives auto-hiding completed content in all three.
    ImGui::TextUnformatted(Tr("WE_OPT_GW2_API_KEY"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Tr("WE_OPT_API_KEY_DELAY_NOTE"));

    {
        static char apiKeyBuf[128] = "";
        static bool bufInitialized = false;
        if (!bufInitialized) //. one-time seed from setting
        {
            strncpy(apiKeyBuf, Gw2ApiKey.c_str(), sizeof(apiKeyBuf) - 1);
            apiKeyBuf[sizeof(apiKeyBuf) - 1] = '\0';
            bufInitialized = true;
        }

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##gw2_api_key", apiKeyBuf, sizeof(apiKeyBuf), ImGuiInputTextFlags_Password))
            Gw2ApiKey = apiKeyBuf;
    }
    Tooltip(Tr("WE_TIP_GW2_API_KEY"));

    ImGui::SameLine();
    switch (GetGw2ApiStatus())
    {
        case Gw2ApiStatus::NoKey:
            ImGui::TextDisabled("%s", Tr("WE_OPT_API_NO_KEY"));
            break;
        case Gw2ApiStatus::Pending:
            ImGui::TextDisabled("%s", Tr("WE_OPT_API_CHECKING"));
            break;
        case Gw2ApiStatus::Ok:
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", Tr("WE_OPT_API_CONNECTED"));
            break;
        case Gw2ApiStatus::InvalidKey:
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", Tr("WE_OPT_API_INVALID_KEY"));
            break;
        case Gw2ApiStatus::NetworkError:
            ImGui::TextColored(kWarningColor, "%s", Tr("WE_OPT_API_NETWORK_ERROR"));
            break;
    }

    //_ Whether the API half of doneToday is consulted at all; the manual mark always still applies.
    ImGui::Checkbox(Tr("WE_OPT_AUTO_MARK_API_DONE"), &Gw2ApiAutoMarkDoneEnabled);
    Tooltip(Tr("WE_TIP_AUTO_MARK_API_DONE"));

    //_ Master switch: drives whether any of the three subscription views auto-surfaces this week's Vault targets.
    ImGui::Checkbox(Tr("WE_OPT_AUTO_TRACK_VAULT"), &WeeklyAutoTrackEnabled);
    Tooltip(Tr("WE_TIP_AUTO_TRACK_VAULT"));

    //_ Color swatch for the weekly Wizard's Vault tracked dot
    DisabledBlock(!WeeklyAutoTrackEnabled)
    {
        SubToggleIndent indent;
        ImGui::ColorEdit4(TrId("WE_OPT_WEEKLY_COLOR", "##weekly_tracking_color").c_str(), WeeklyAutoTrackColor, kSwatchFlags);
    }
}

//_ Set by RequestOpenAccountHeader, cleared by the frame that consumes it.
static bool s_openAccountHeader = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestOpenAccountHeader   (see: options_general.h)
//--------------------------------------------------------------------------------
void RequestOpenAccountHeader()
{
    s_openAccountHeader = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsGeneral   (see: options_general.h)
//--------------------------------------------------------------------------------
void DrawOptionsGeneral()
{
    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_COMPETITIVE", kDrawCompetitiveModeId).c_str()))
        DrawCompetitiveMode();

    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_SUBS_WINDOW", kDrawSubscriptionsWindowId).c_str()))
        DrawSubscriptionsWindow();

    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_SUBS_BAR", kDrawSubscriptionsBarId).c_str()))
        DrawSubscriptionsBar();

    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_TOAST", kDrawToastPopupsId).c_str()))
        DrawToastPopups();

    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_CHAT", kDrawChatAndPasteId).c_str()))
        DrawChatAndPaste();

    if (s_openAccountHeader)
        ImGui::SetNextItemOpen(true);
    bool accountOpen = ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_ACCOUNT", kDrawAccountAndTrackingId).c_str());
    if (s_openAccountHeader)
    {
        ImGui::SetScrollHereY(0.0f); //. align header to top edge
        s_openAccountHeader = false;
    }
    if (accountOpen)
        DrawAccountAndTracking();
}