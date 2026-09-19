//################################################################################
// addon_options.cpp
//--------------------------------------------------------------------------------
// AddonOptions()   draws the World Events section of the Nexus options panel
//--------------------------------------------------------------------------------
// Nexus UI callback - draws into a panel Nexus owns, not a standalone window.
// Widgets write directly into the global settings (settings.h / settings_table.h)
// or into g_Events / g_CyclicGroups / g_BasicCategories / g_CyclicCategories.
// There is no explicit "Save" button: everything is writtento disk on AddonUnload
// (see addon.cpp), so edits here just live in memory until the addon (or the
// game) closes.
//
// Covers all the flat scalar settings (overlay visibility, ring radius/thickness,
// entry/exit window) and full editing of individual events cyclic groups/slots,
// and categories - creating, renaming, deleting, recoloring, drag-and-drop
// categorization, and icon assignment.
//
// The widget-drawing helpers themselves (scoped-disable, period widget,
// icon/color pickers, duplicate-name checks, drag-and-drop plumbing, the notify-
// level control, the shared name/context-menu row, search predicates, and the two
// full row drawers) live in addon_options_helpers.h/.cpp - this file is just
// AddonOptions() itself, assembling those pieces into the panel layout.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "addon_options_helpers.h"
#include "better_chat.h"
#include "build_info.h"
#include "changelog_window.h"
#include "events.h"
#include "events_categories.h"
#include "events_live.h"
#include "events_storage.h"   //. SlugifyName/UniqueId for new categories
#include "events_tracking.h"
#include "gw2_api.h"
#include "icon_whitener.h"
#include "imgui.h"
#include "live_events_ui.h"
#include "localization.h"
#include "maprender.h" //. ScreenFractionToPixels/PixelsToScreenFraction, for the toast position row
#include "notify_sound.h"
#include "reset_defaults.h"
#include "settings.h"
#include "subscriptions_ui.h" //. RequestNotificationLayoutPreview, for the toast width/position/direction rows
#include "ws_debug_window.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonOptions
//--------------------------------------------------------------------------------
// Laid out as three stacked BeginTable/EndTable pairs plus two full-width
// CollapsingHeaders (one wrapping Table 2+3, one nested around just the search
// box and Table 3) - a CollapsingHeader clips to a single table column, so it
// can't be drawn inside either table. List mutations (add/remove event, group,
// category) are captured as bools during the row loop and applied afterward, to
// avoid invalidating indices mid-iteration. One search box filters both the Basic
// and Cyclic trees at once.
//--------------------------------------------------------------------------------
void AddonOptions()
{
    OptionsRenderTimer optionsRenderTimer; //. no-op unless ShowDebug
    ImVec2 dummySquare = ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight());
    
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    if (ImGui::SmallButton(Tr("WE_CHANGELOG_TITLE")))
        ShowVersionHistoryWindow = true;
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::TextDisabled("%s: %s", Tr("WE_OPT_RELEASE"), DateAndTime.c_str());
    ImGui::SameLine();
    
    if constexpr (ShowDebug)
    {
        //_ "Render" = AddonRender's own cost (rings/bar/window/notify); "Options UI" = this panel's per-frame cost.
        ImGui::TextDisabled("Render: %.3f ms avg (1s)", g_AvgRenderTimeMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Options UI: %.3f ms avg (1s)", g_AvgOptionsRenderTimeMs);

        //_ Per-view Data (cache refresh) vs Draw (pixel work) split, so "why is view X slow" maps to one number.
        ImGui::TextDisabled("Bar: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsBarDataMs, g_AvgSubsBarDrawMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Window: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsWindowDataMs, g_AvgSubsWindowDrawMs);
        ImGui::TextDisabled("Notify: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsNotifyDataMs, g_AvgSubsNotifyDrawMs);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    //_ Master is a derived AND of the three settings, not its own.
    bool disableAllCompetitive = DisableWindowWhenCompetitive && DisableBarWhenCompetitive && DisableNotifyWhenCompetitive;
    if (ImGui::Checkbox(Tr("WE_OPT_DISABLE_COMPETITIVE"), &disableAllCompetitive))
    {
        DisableWindowWhenCompetitive = disableAllCompetitive;
        DisableBarWhenCompetitive    = disableAllCompetitive;
        DisableNotifyWhenCompetitive = disableAllCompetitive;
    }
    Tooltip(Tr("WE_TIP_DISABLE_COMPETITIVE"));

    ImGui::SameLine();
    ImGui::Checkbox(TrId("WE_OPT_WINDOW", "##dis_comp_window").c_str(), &DisableWindowWhenCompetitive);
    ImGui::SameLine();
    ImGui::Checkbox(TrId("WE_OPT_TOAST", "##dis_comp_toast").c_str(), &DisableNotifyWhenCompetitive);
    ImGui::SameLine();
    ImGui::Checkbox(TrId("WE_OPT_BAR", "##dis_comp_bar").c_str(), &DisableBarWhenCompetitive);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(Tr("WE_OPT_OVERLAY_SETTINGS")))
    {
        //_ Table 1 - Subscriptions, always visible; split out since CollapsingHeader can't span table columns.
        if (ImGui::BeginTable("##subs_table", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextRow();

            //_ Column 0: Subscriptions window, then Notification popups
            ImGui::TableSetColumnIndex(0);

            //_ Watchlist window toggle only opens/closes the window, not which events are subscribed (events.json data).
            ImGui::Checkbox(Tr("WE_OPT_SHOW_SUBS_WINDOW"), &ShowSubscriptionsWindow);
            DisabledBlock(!ShowSubscriptionsWindow)
            {
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_IN_WINDOW"), &SubscriptionsHideActive);
                
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();

                //_ RGB only (feeds TextColored), not a tinted dot/icon like BasicEventColor* below, which need alpha.
                ImGui::ColorEdit3(TrId("WE_OPT_ACTIVE", "##sub_color_active").c_str(), SubscriptionsActiveColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit3(TrId("WE_OPT_SOON", "##sub_color_soon").c_str(), SubscriptionsSoonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }

            ImGui::Dummy(dummySquare);

            //_ Third, independent view of the same subscription data (toast popups); not gated by window/bar visibility.
            ImGui::Checkbox(Tr("WE_OPT_ENABLE_NOTIFY_POPUPS"), &NotificationsEnabled);
            Tooltip(Tr("WE_TIP_NOTIFY_POPUPS"));

            DisabledBlock(!NotificationsEnabled)
            {
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::InputInt(Tr("WE_OPT_WARN_BEFORE_START"), &NotificationLeadMinutes, 0, 0))
                {
                    //_ 0 is a valid value ("off"); floor is 0, not 1.
                    if (NotificationLeadMinutes < 0)   NotificationLeadMinutes = 0;
                    if (NotificationLeadMinutes > 120) NotificationLeadMinutes = 120;
                }
                Tooltip(Tr("WE_TIP_WARN_BEFORE_START"));
                    
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_NOTIFY_ON_START"), &NotificationOnStart);

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::InputInt(Tr("WE_OPT_POPUP_DURATION"), &NotificationDisplaySeconds, 0, 0))
                {
                    if (NotificationDisplaySeconds < 1)   NotificationDisplaySeconds = 1;
                    if (NotificationDisplaySeconds > 120) NotificationDisplaySeconds = 120;
                }
                Tooltip(Tr("WE_TIP_POPUP_DURATION"));

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                //_ DragFloat, same speed/behavior as the unsafe-zone DragInt rows below, but on the underlying float directly - no int round-trip needed since the setting itself is a float.
                ImGui::SetNextItemWidth(60);
                if (ImGui::DragFloat(Tr("WE_OPT_TOAST_WIDTH"), &NotificationPopupWidth, 1, 0, 0, "%.0fpx"))
                {
                    if (NotificationPopupWidth < 100.0f) NotificationPopupWidth = 100.0f;
                    if (NotificationPopupWidth > 800.0f) NotificationPopupWidth = 800.0f;
                }
                if (ItemPreviewGate()) RequestNotificationLayoutPreview();
                Tooltip(Tr("WE_TIP_TOAST_WIDTH"));

                //_ Shown/edited as pixels, stored as a screen fraction - same convention as DrawFixToScreenRow (addon_options_helpers.cpp).
                //_ DragFloat2 rather than DragFloat (only one X/Y widget, like the unsafe-zone left/right pair split across two).
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
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

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_TOAST_STACK_UP"), &NotificationStackUpward);
                if (ItemPreviewGate()) RequestNotificationLayoutPreview();
                Tooltip(Tr("WE_TIP_TOAST_STACK_UP"));

                //_ Single .wav file, picked from "<addon dir>/sounds"; which events play it is each row's notify level.
                {
                    const std::vector<std::string>& soundFiles = GetNotificationSoundFilenames();

                    //_ Reuses DrawSpeakerIcon (notify level 3's icon) to mark this row as about the notification sound.
                    {
                        float sq = ImGui::GetFrameHeight();
                        ImVec2 rmin = ImGui::GetCursorScreenPos();
                        ImVec2 center(rmin.x + sq * 0.5f, rmin.y + sq * 0.5f);
                        DrawSpeakerIcon(ImGui::GetWindowDrawList(), center, sq * 0.96f, ImGui::GetColorU32(ImGuiCol_Text));
                        ImGui::Dummy(ImVec2(sq, sq));
                    }
                    ImGui::SameLine();

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
                }
            }

            //_ Column 1: Subscriptions bar
            ImGui::TableSetColumnIndex(1);

            //_ Second, alternate view of subscription data: a thin animated line pinned to the screen edge, no titlebar.
            ImGui::Checkbox(Tr("WE_OPT_SHOW_SUBS_BAR"), &ShowSubscriptionsBar);

            DisabledBlock(!ShowSubscriptionsBar)
            {
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_HIDE_ACTIVE_ON_BAR"), &SubscriptionsBarHideActive);
                Tooltip(Tr("WE_TIP_HIDE_ACTIVE_ON_BAR"));

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_MINIMAL_MODE"), &SubscriptionsBarMinimalMode);
                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_OPT_BOTTOM_LINE"), &SubscriptionsBarBottomAnchored);
                
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::ColorEdit4(TrId("WE_OPT_DOT_COLOR", "##bar_dot_color").c_str(), SubscriptionsBarDotColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::InputInt(Tr("WE_OPT_POPOUT_HEIGHT"), &SubscriptionsBarMaxDropPx, 0, 0))
                {
                    //_ Floored at 8, not 0: subscriptions_bar.cpp derives the pill's corner radius from half this value.
                    if (SubscriptionsBarMaxDropPx < 8)     SubscriptionsBarMaxDropPx = 8;
                    if (SubscriptionsBarMaxDropPx > 300)   SubscriptionsBarMaxDropPx = 300;
                }
                Tooltip(Tr("WE_TIP_POPOUT_HEIGHT"));
                    
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::InputInt(Tr("WE_OPT_POPOUT_DELAY"), &SubscriptionsBarHoverDelayMs, 0, 0))
                {
                    //_ Clamped post-hoc - InputInt allows transient out-of-range input; 0 is valid.
                    if (SubscriptionsBarHoverDelayMs < 0)    SubscriptionsBarHoverDelayMs = 0;
                    if (SubscriptionsBarHoverDelayMs > 5000) SubscriptionsBarHoverDelayMs = 5000;
                }
                Tooltip(Tr("WE_TIP_POPOUT_DELAY"));

                float screenWidth = ImGui::GetIO().DisplaySize.x;
                float screenHeight = ImGui::GetIO().DisplaySize.y;

                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::Text("%s", Tr("WE_OPT_UNSAFE_ZONE"));
                
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::DragInt(TrId("WE_OPT_LEFT", "##leftuz").c_str(), &SubscriptionsBarUnsafeLeftPx, 1, 0,0, "%dpx"))
                {
                    if (SubscriptionsBarUnsafeLeftPx < 0)           SubscriptionsBarUnsafeLeftPx = 0;
                    if (SubscriptionsBarUnsafeLeftPx > screenWidth)  SubscriptionsBarUnsafeLeftPx = (int)screenWidth;
                    if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                        SubscriptionsBarUnsafeRightPx = (int)screenWidth - SubscriptionsBarUnsafeLeftPx;
                }
                bool leftActive = ItemPreviewGate();
                Tooltip(Tr("WE_TIP_UNSAFE_LEFT"));
                
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::DragInt(TrId("WE_OPT_RIGHT", "##rightuz").c_str(), &SubscriptionsBarUnsafeRightPx, 1, 0, 0, "%dpx"))
                {
                    if (SubscriptionsBarUnsafeRightPx < 0)           SubscriptionsBarUnsafeRightPx = 0;
                    if (SubscriptionsBarUnsafeRightPx > screenWidth)  SubscriptionsBarUnsafeRightPx = (int)screenWidth;
                    if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                        SubscriptionsBarUnsafeLeftPx = (int)screenWidth - SubscriptionsBarUnsafeRightPx;
                }
                bool rightActive = ItemPreviewGate();
                Tooltip(Tr("WE_TIP_UNSAFE_RIGHT"));
                
                ImGui::Dummy(dummySquare);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::DragInt(TrId("WE_OPT_HEIGHT_LEFT", "##heightuzleft").c_str(), &SubscriptionsBarUnsafeHeightLeftPx, 1, 0, 0, "%dpx"))
                {
                    if (SubscriptionsBarUnsafeHeightLeftPx < 0)    SubscriptionsBarUnsafeHeightLeftPx = 0;
                    if (SubscriptionsBarUnsafeHeightLeftPx > screenHeight) SubscriptionsBarUnsafeHeightLeftPx = (int)screenHeight;
                }
                bool heightLeftActive = ItemPreviewGate();
                Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_LEFT"));

                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::DragInt(TrId("WE_OPT_HEIGHT_RIGHT", "##heightuzright").c_str(), &SubscriptionsBarUnsafeHeightRightPx, 1, 0, 0, "%dpx"))
                {
                    if (SubscriptionsBarUnsafeHeightRightPx < 0)    SubscriptionsBarUnsafeHeightRightPx = 0;
                    if (SubscriptionsBarUnsafeHeightRightPx > screenHeight) SubscriptionsBarUnsafeHeightRightPx = (int)screenHeight;
                }
                bool heightRightActive = ItemPreviewGate();
                Tooltip(Tr("WE_TIP_UNSAFE_HEIGHT_RIGHT"));
                
                //_ Live preview, shown while one of the four fields above is active or hovered (ItemPreviewGate, addon_options_helpers.h); mirrors subscriptions_bar.cpp's anchor math.
                if (leftActive || rightActive || heightLeftActive || heightRightActive)
                {
                    ImDrawList* dl = ImGui::GetForegroundDrawList();
                    const ImU32 kYellow = IM_COL32(255, 220, 0, 255);
                    const float kDropDir  = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
                    const float kBaselineY = SubscriptionsBarBottomAnchored
                        ? (ImGui::GetIO().DisplaySize.y - 1.0f)
                        : 1.0f;
                    const float hLeft  = kBaselineY + kDropDir * (float)SubscriptionsBarUnsafeHeightLeftPx;
                    const float hRight = kBaselineY + kDropDir * (float)SubscriptionsBarUnsafeHeightRightPx;
                
                    //_ Left zone: vertical edge + horizontal top from the screen edge to it
                    dl->AddLine(ImVec2((float)SubscriptionsBarUnsafeLeftPx, kBaselineY),
                                ImVec2((float)SubscriptionsBarUnsafeLeftPx, hLeft), kYellow, 2.0f);
                    dl->AddLine(ImVec2(0.0f, hLeft),
                                ImVec2((float)SubscriptionsBarUnsafeLeftPx, hLeft), kYellow, 2.0f);
                
                    //_ Right zone: mirrored, its own height
                    float xRight = screenWidth - (float)SubscriptionsBarUnsafeRightPx;
                    dl->AddLine(ImVec2(xRight, kBaselineY), ImVec2(xRight, hRight), kYellow, 2.0f);
                    dl->AddLine(ImVec2(xRight, hRight), ImVec2(screenWidth, hRight), kYellow, 2.0f);
                }
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader(Tr("WE_OPT_EVENTS_SETTINGS_HEADER")))
    {
        //_ Table 2 - Search/API key (Row 1) and section controls (Row 2); exists only while the header is expanded.
        if (ImGui::BeginTable("##world_events_table", 2, ImGuiTableFlags_SizingStretchSame))
        {
            //_ Row 1 - Search/Paste (col 0), API key/tracking (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("%s", Tr("WE_OPT_CHAT_SETTINGS"));
            static bool unlockDelay = false;
            ImGui::Checkbox("##lock_delay", &unlockDelay);
            Tooltip(Tr("WE_TIP_UNLOCK_PASTE_DELAY"));
            ImGui::SameLine();
            DisabledBlock(!unlockDelay)
            {
                ImGui::SetNextItemWidth(50.0f);
                ImGui::InputInt(Tr("WE_OPT_PASTE_DELAY"), &delayMilliseconds, 0 , 0);
            }

            {
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
                        
                if (!IsBetterChatLoaded()) ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_NOT_LOADED"));
                else if (!IsBetterChatSelfCommandEnabled()) ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_SELF_DISABLED"));
                else if (IsBetterChatSelfCommandEnabled()) ImGui::TextDisabled("%s", Tr("WE_OPT_BETTER_CHAT_SELF_ENABLED"));
            }
            
            ImGui::Dummy(dummySquare);
            
            //_ Zoom-based marker scaling; disabled by default keeps the old fixed-size behavior, just optional now.
            {
                ImGui::Checkbox(TrId("WE_OPT_GROW_MARKERS_ZOOM", "##basic_zoom_scaling_enabled").c_str(), &BasicEventZoomScalingEnabled);
    
                DisabledBlock(!BasicEventZoomScalingEnabled)
                {
                    ImGui::Dummy(dummySquare);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat(TrId("WE_OPT_START_GROWING_AT", "##basic_zoom_start_pct").c_str(), &BasicEventZoomStartPct, 1.0f, 0.0f, 100.0f, "%.0f%%");
                    ImGui::Dummy(dummySquare);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat(TrId("WE_OPT_MAX_SIZE_AT_ZOOM", "##basic_zoom_max_mult").c_str(), &BasicEventZoomMaxMultiplier, 1.0f, 1.0f, 4.0f, "%.1fx");
                }
            }

            ImGui::TableSetColumnIndex(1);
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
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", Tr("WE_OPT_API_NETWORK_ERROR"));
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
                ImGui::ColorEdit4(TrId("WE_OPT_WEEKLY_COLOR", "##weekly_tracking_color").c_str(), WeeklyAutoTrackColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }
            
            ImGui::Dummy(dummySquare);

            //_ Manual counterpart to the API-based hiding above; covers everything the API doesn't, key or no key.
            static bool unlockMarkers = false;
            ImGui::Checkbox("##lock_markers", &unlockMarkers);
            Tooltip(Tr("WE_TIP_UNLOCK_DONE_MARKERS"));
            ImGui::SameLine();
            DisabledBlock(!unlockMarkers)
            {
                if (ImGui::Button(Tr("WE_OPT_CLEAR_DONE_MARKERS")))
                    ClearAllDoneMarkers();
            }
            //_ Row 2 - Basic Events controls (col 0), Cyclic Events controls (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            //_ Only affects upcoming Basic Events (active always show); not offered for cyclic groups.
            {
                int mins = BasicEventTimeFilterMinutes;
                int h    = mins / 60;
                int m    = mins % 60;
            
                char label[96];
                if (h > 0)
                    snprintf(label, sizeof(label), "%dh %02dm", h, m);
                else
                    snprintf(label, sizeof(label), "%dm", m);
            
                ImGui::Checkbox(TrId("WE_OPT_ONLY_SHOW_STARTING_IN", "##basic_time_filter_enabled").c_str(), &BasicEventTimeFilterEnabled);
            
                if (BasicEventTimeFilterEnabled)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                
                    int stepIndex = BasicEventTimeFilterMinutes / 15;
                    if (ImGui::DragInt("##basic_time_filter_minutes", &stepIndex, 0.2f, 0, 48, label, ImGuiSliderFlags_NoInput))
                    {
                        BasicEventTimeFilterMinutes = stepIndex * 15;
                    }
                }
            }

            //_ One shared color set for every Basic Event, matching the active/soon/waiting dot and icon-tint states.
            {
                ImGui::ColorEdit4(TrId("WE_OPT_ACTIVE", "##basic_color_active").c_str(), BasicEventColorActive, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4(TrId("WE_OPT_SOON", "##basic_color_soon").c_str(), BasicEventColorSoon, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4(TrId("WE_OPT_WAITING", "##basic_color_waiting").c_str(), BasicEventColorWaiting, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }

            //_ Independent settings, not derived from one another - dot and icon sizes can differ freely.
            {
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(TrId("WE_OPT_DOT_RADIUS", "##basic_dot_radius").c_str(), &BasicEventDotRadius, 1.0f, 2.0f, 30.0f, "%.0f px");

                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(TrId("WE_OPT_ICON_SIZE", "##basic_icon_size").c_str(), &BasicEventIconSize, 1.0f, 2.0f, 40.0f, "%.0f px");
            }

            DrawIconWhitenerButton();   //. opens the Icon Whitener modal
            DrawIconWhitenerPopup();    //. renders modal, no-op if closed

            ImGui::TableSetColumnIndex(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox(Tr("WE_OPT_SHOW_CYCLIC_ON_MAP"), &ShowCyclicOverlay);
            DisabledBlock(!ShowCyclicOverlay)
            {
                ImGui::TextUnformatted(Tr("WE_OPT_RING_APPEARANCE"));
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(Tr("WE_OPT_RADIUS"), &CyclicRadius, 1.0f, 5.0f, 50.0f, "%.0f px");
                if ( CyclicRadius < CyclicThickness / 2 ) { CyclicThickness = CyclicRadius * 2; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(Tr("WE_OPT_THICKNESS"), &CyclicThickness, 1.0f, 5.0f, 100.0f, "%.0f px");
                if ( CyclicThickness > CyclicRadius * 2 ) { CyclicRadius = CyclicThickness / 2; }

                ImGui::TextUnformatted(Tr("WE_OPT_ENTRY_EXIT_WINDOW"));
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(Tr("WE_OPT_FUTURE_WINDOW"), &CyclicMaxFutureDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
                if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxPastDeg = 360 - CyclicMaxFutureDeg; }
                Tooltip(Tr("WE_TIP_FUTURE_WINDOW"));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat(Tr("WE_OPT_PAST_WINDOW"), &CyclicMaxPastDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
                if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxFutureDeg = 360 - CyclicMaxPastDeg; }
                Tooltip(Tr("WE_TIP_PAST_WINDOW"));

                ImGui::Checkbox(TrId("WE_OPT_FADE_PAST_EVENTS", "##cyclic_past_fade_enabled").c_str(), &CyclicPastFadeEnabled);
                Tooltip(Tr("WE_TIP_FADE_PAST_EVENTS"));

                ImGui::TextUnformatted(Tr("WE_OPT_HAND"));
                ImGui::ColorEdit4(TrId("WE_OPT_COLOR", "##cyclic_hand_color").c_str(), CyclicHandColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                Tooltip(Tr("WE_TIP_HAND_COLOR"));
                
                ImGui::SameLine();
                ImGui::Checkbox(TrId("WE_OPT_USE_TEXTURE", "##cyclic_hand_image_enabled").c_str(), &CyclicHandImageEnabled);
                Tooltip(Tr("WE_TIP_HAND_USE_TEXTURE"));

                DisabledBlock(!CyclicHandImageEnabled)
                {
                    //_ Same source list/folder as the Basic Event icon picker and the Ring edge image below.
                    const std::vector<std::string>& handIconFiles = GetEventIconFilenames();
                    std::vector<const char*> handIconLabels;
                    handIconLabels.push_back(Tr("WE_OPT_NONE"));
                    for (const auto& fn : handIconFiles)
                        handIconLabels.push_back(fn.c_str());

                    int handIconIndex = 0;
                    if (!CyclicHandImageFilename.empty())
                        for (int k = 0; k < (int)handIconFiles.size(); k++)
                            if (handIconFiles[k] == CyclicHandImageFilename)
                                handIconIndex = k + 1;

                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::Combo("##cyclic_hand_image_file", &handIconIndex, handIconLabels.data(), (int)handIconLabels.size()))
                        CyclicHandImageFilename = (handIconIndex == 0) ? std::string() : handIconFiles[handIconIndex - 1];

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    ImGui::DragFloat(TrId("WE_OPT_WIDTH", "##cyclic_hand_image_width").c_str(), &CyclicHandImageWidth, 1.0f, 2.0f, 60.0f, "%.0f px");
                    Tooltip(Tr("WE_TIP_HAND_TEXTURE_WIDTH"));
                }

                ImGui::Spacing();
                ImGui::TextUnformatted(Tr("WE_OPT_RING_EDGE_TEXTURE"));
                ImGui::Checkbox("##cyclic_ring_image_enabled", &CyclicRingImageEnabled);
                Tooltip(Tr("WE_TIP_RING_EDGE_TEXTURE"));

                DisabledBlock(!CyclicRingImageEnabled)
                {
                    //_ Same source list as the Basic Event icon picker (maprender.h); "None" replaces "Dot" - no fallback shape here.
                    const std::vector<std::string>& iconFiles = GetEventIconFilenames();
                    std::vector<const char*> iconLabels;
                    iconLabels.push_back(Tr("WE_OPT_NONE"));
                    for (const auto& fn : iconFiles)
                        iconLabels.push_back(fn.c_str());

                    int iconIndex = 0;
                    if (!CyclicRingImageFilename.empty())
                        for (int k = 0; k < (int)iconFiles.size(); k++)
                            if (iconFiles[k] == CyclicRingImageFilename)
                                iconIndex = k + 1;
                                
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::Combo(TrId("WE_OPT_TEXTURE", "##cyclic_ring_image_file").c_str(), &iconIndex, iconLabels.data(), (int)iconLabels.size()))
                        CyclicRingImageFilename = (iconIndex == 0) ? std::string() : iconFiles[iconIndex - 1];

                    //_ Its own row - the ONLY control over on-screen band thickness (CyclicRingImageThickness).
                    ImGui::SetNextItemWidth(50.0f);
                    ImGui::DragFloat(TrId("WE_OPT_THICKNESS", "##cyclic_ring_image_thickness").c_str(), &CyclicRingImageThickness, 0.5f, 1.0f, 80.0f, "%.1f px");
                    Tooltip(Tr("WE_TIP_RING_TEXTURE_THICKNESS"));
                            
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    ImGui::DragFloat(TrId("WE_OPT_OFFSET", "##cyclic_ring_image_offset").c_str(), &CyclicRingImageOffset, 0.1f, -5.0f, 5.0f, "%.1f px");
                    Tooltip(Tr("WE_TIP_RING_TEXTURE_OFFSET"));
                }

                ImGui::Spacing();
                ImGui::TextUnformatted(Tr("WE_OPT_FILL_TEXTURE"));
                ImGui::Checkbox("##cyclic_fill_image_enabled", &CyclicFillImageEnabled);
                Tooltip(Tr("WE_TIP_FILL_TEXTURE"));

                DisabledBlock(!CyclicFillImageEnabled)
                {
                    //_ Same source list/folder as the other icon pickers above.
                    const std::vector<std::string>& fillIconFiles = GetEventIconFilenames();
                    std::vector<const char*> fillIconLabels;
                    fillIconLabels.push_back(Tr("WE_OPT_NONE"));
                    for (const auto& fn : fillIconFiles)
                        fillIconLabels.push_back(fn.c_str());

                    int fillIconIndex = 0;
                    if (!CyclicFillImageFilename.empty())
                        for (int k = 0; k < (int)fillIconFiles.size(); k++)
                            if (fillIconFiles[k] == CyclicFillImageFilename)
                                fillIconIndex = k + 1;
                                
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::Combo("##cyclic_fill_image_file", &fillIconIndex, fillIconLabels.data(), (int)fillIconLabels.size()))
                        CyclicFillImageFilename = (fillIconIndex == 0) ? std::string() : fillIconFiles[fillIconIndex - 1];
                        
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    ImGui::DragFloat(TrId("WE_OPT_OPACITY", "##cyclic_fill_image_opacity").c_str(), &CyclicFillImageOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
                }
            }
            
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader(Tr("WE_OPT_EVENT_LISTS_HEADER")))
    {
        //_ Transient UI state (not persisted); filters both trees - event name for Basic, group+slot for Cyclic.
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText(TrId("WE_OPT_SEARCH_LABEL", "##global_search").c_str(), searchBuf, sizeof(searchBuf));
        std::string searchQueryLower = searchBuf;
        std::transform(searchQueryLower.begin(), searchQueryLower.end(), searchQueryLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        bool searchActive = !searchQueryLower.empty();

        ImGui::SameLine();
        DrawResetToDefaultsButton();
        DrawResetToDefaultsPopup(); //. no-op unless the confirm popup is open

        ImGui::SameLine();
        DrawRestoreMissingButton();

        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr("WE_OPT_RIGHT_CLICK_HINT"));

        //_ Table 3 - Basic Events tree (col 0), Cyclic Events tree (col 1); split out so search can filter both.
        if (ImGui::BeginTable("##world_events_data", 2, ImGuiTableFlags_SizingStretchSame))
        {
            //_ Row 3 - Basic Events tree (col 0), Cyclic Events tree (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            //_ Basic Events header + add buttons; add/remove is deferred until after the tree loop to avoid index invalidation.
            ImGui::TextUnformatted(Tr("WE_OPT_BASIC_EVENTS"));
            MakeDropTarget(kBasicEventDragType, g_BasicCategories, -1);
            ImGui::SameLine();
            bool pendingAdd = false;
            DisabledBlock(IsBasicEventCreationPending())
            {
                pendingAdd = ImGui::SmallButton("+##add_basic_event");
            }
            if (IsBasicEventCreationPending() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted(Tr("WE_OPT_CATEGORIES"));
            ImGui::SameLine();
            //_ Persists (unlike s_pendingBasicCategoryFocus below) until that category is saved/cancelled.
            static int s_newBasicCategoryIndex = -1;
            bool pendingAddBasicCategory = false;
            DisabledBlock(s_newBasicCategoryIndex >= 0)
            {
                pendingAddBasicCategory = ImGui::SmallButton("+##add_basic_category");
            }
            if (s_newBasicCategoryIndex >= 0 && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));
        
            //_ Section-level bulk icon picker, applies to every Basic Event regardless of category; no per-category one.
            {
                std::vector<int> allIndices(g_Events.size());
                for (int bi = 0; bi < (int)g_Events.size(); bi++) allIndices[bi] = bi;
                ImGui::SetNextItemWidth(100.0f);
                DrawBulkIconPicker(TrId("WE_OPT_SET_ALL_ICONS", "###bulk_icon_all").c_str(), allIndices);
            }

            int pendingRemoveIndex = -1;
            int pendingRemoveBasicCategoryIndex = -1;
            static std::map<int, std::string> editingBasicCategoryNames;
            //_ One-shot; set on push, consumed next draw - same pattern as RequestBasicEventNameEdit, but local since add and draw both happen here.
            static int s_pendingBasicCategoryFocus = -1;

            std::vector<bool> isCategorized(g_Events.size(), false);

            //_ Category-aware draw order: each category's members draw first (nested), then leftovers uncategorized.
            for (int c = 0; c < (int)g_BasicCategories.size(); c++)
            {
                Category& cat = g_BasicCategories[c];
                ImGui::PushID(1000000 + c); //. offset clear of event indices

                //_ Resolved once up front: reused by the bulk icon picker and the membership loop instead of re-searching.
                std::vector<int> memberIndices;
                for (const std::string& memberId : cat.members)
                    for (int mi = 0; mi < (int)g_Events.size(); mi++)
                        if (g_Events[mi].id == memberId) { memberIndices.push_back(mi); break; }

                bool categoryNameMatches = ContainsCaseInsensitive(DisplayName(cat, CategoryListKind::Basic), searchQueryLower);
                bool categoryHasMatch = categoryNameMatches;
                if (!categoryHasMatch)
                    for (int mi : memberIndices)
                        if (EventMatchesSearch(g_Events[mi], searchQueryLower))
                            categoryHasMatch = true;

                //_ Set before TreeNode draws (SetNextItemOpen must go first) - starts false, set only when it draws.
                bool catOpen = false;
                if (!searchActive || categoryHasMatch)
                {
                    if (searchActive)
                        ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

                    std::string oldCategoryName = DisplayName(cat, CategoryListKind::Basic);
                    bool categoryAutoFocus = (s_pendingBasicCategoryFocus == c);
                    if (categoryAutoFocus)
                    {
                        editingBasicCategoryNames[c] = ""; //. freshly created - starts empty, forces the inline editor open
                        s_pendingBasicCategoryFocus = -1;
                    }
                    bool categoryIsNew = (s_newBasicCategoryIndex == c);
                    NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, oldCategoryName, editingBasicCategoryNames, pendingRemoveBasicCategoryIndex,
                        nullptr, std::string(), nullptr, nullptr, -1, nullptr, nullptr, true, categoryAutoFocus, categoryIsNew);
                    catOpen = nameResult.open;
                    MakeDropTarget(kBasicEventDragType, g_BasicCategories, c);
                    if (nameResult.newName != oldCategoryName)
                    {
                        //_ No rename-patching needed - members and forced-membership both reference the category by id, never customName.
                        cat.customName = nameResult.newName;
                    }
                    //_ Resolved (saved or cancelled) - frees the "+" button back up.
                    if (categoryIsNew && (nameResult.cancelled || nameResult.newName != oldCategoryName))
                        s_newBasicCategoryIndex = -1;
                }

                //_ Bookkeeping (isCategorized) runs even when catOpen is false, so a folded category can't leak members.
                for (int mi : memberIndices)
                {
                    isCategorized[mi] = true;

                    bool memberMatches = categoryNameMatches || EventMatchesSearch(g_Events[mi], searchQueryLower);

                    if (catOpen && memberMatches)
                    {
                        ImGui::PushID(mi);
                        DrawBasicEventRow(mi, pendingRemoveIndex);
                        ImGui::PopID();
                    }
                }

                if (catOpen)
                {
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            for (int i = 0; i < (int)g_Events.size(); i++)
            {
                if (isCategorized[i]) continue;
                if (!EventMatchesSearch(g_Events[i], searchQueryLower)) continue;

                ImGui::PushID(i);
                DrawBasicEventRow(i, pendingRemoveIndex);
                ImGui::PopID();
            }

            if (pendingRemoveIndex >= 0)
                g_Events.erase(g_Events.begin() + pendingRemoveIndex);

            if (pendingAdd)
            {
                std::unordered_set<std::string> usedIds;
                for (const auto& ev : g_Events) usedIds.insert(ev.id);

                WorldEvent newEvent{};
                //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
                newEvent.id         = UniqueId(SlugifyName("event"), usedIds);
                newEvent.customName = ""; //. starts unnamed - forces the inline editor open on next draw (RequestBasicEventNameEdit below)
                newEvent.continentX = 49332.0f;
                newEvent.continentY = 31457.0f;
                newEvent.isVarying  = false;
                newEvent.duration   = 900;  //. 15 min, a reasonable default
                newEvent.period     = 7200; //. 2h, most common period
                newEvent.offset     = 0;
                g_Events.push_back(newEvent);
                RequestBasicEventNameEdit((int)g_Events.size() - 1);
            }

            if (pendingRemoveBasicCategoryIndex >= 0)
                g_BasicCategories.erase(g_BasicCategories.begin() + pendingRemoveBasicCategoryIndex);

            if (pendingAddBasicCategory)
            {
                std::unordered_set<std::string> usedIds;
                for (const auto& c : g_BasicCategories) usedIds.insert(c.id);

                Category newCat;
                //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
                newCat.id         = UniqueId(SlugifyName("category"), usedIds);
                newCat.customName = ""; //. starts unnamed - forces the inline editor open on next draw
                g_BasicCategories.push_back(newCat);
                s_pendingBasicCategoryFocus = (int)g_BasicCategories.size() - 1;
                s_newBasicCategoryIndex     = s_pendingBasicCategoryFocus;
            }

            ImGui::TableSetColumnIndex(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        
            //_ Cyclic Events header + add buttons; same deferred add/remove pattern as Basic Events above.
            ImGui::TextUnformatted(Tr("WE_OPT_CYCLIC_EVENTS"));
            MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, -1); //. drop here to uncategorize
            ImGui::SameLine();
            bool pendingAddGroup = false;
            DisabledBlock(IsCyclicGroupCreationPending())
            {
                pendingAddGroup = ImGui::SmallButton("+##add_cyclic_group");
            }
            if (IsCyclicGroupCreationPending() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted(Tr("WE_OPT_CATEGORIES"));
            ImGui::SameLine();
            //_ Persists (unlike s_pendingCyclicCategoryFocus below) until that category is saved/cancelled.
            static int s_newCyclicCategoryIndex = -1;
            bool pendingAddCyclicCategory = false;
            DisabledBlock(s_newCyclicCategoryIndex >= 0)
            {
                pendingAddCyclicCategory = ImGui::SmallButton("+##add_cyclic_category");
            }
            if (s_newCyclicCategoryIndex >= 0 && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));

            int pendingRemoveGroupIndex = -1;
            int pendingRemoveCyclicCategoryIndex = -1;
            static std::map<int, std::string> editingCyclicCategoryNames;
            //_ One-shot, same pattern as s_pendingBasicCategoryFocus above.
            static int s_pendingCyclicCategoryFocus = -1;

            std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

            //_ Same category-aware draw order as Basic Events above.
            for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
            {
                Category& cat = g_CyclicCategories[c];
                ImGui::PushID(2000000 + c); //. offset clear of other indices

                bool categoryNameMatches = ContainsCaseInsensitive(DisplayName(cat, CategoryListKind::Cyclic), searchQueryLower);
                bool categoryHasMatch = categoryNameMatches;
                if (!categoryHasMatch)
                    for (const std::string& memberId : cat.members)
                        for (const auto& grp : g_CyclicGroups)
                            if (grp.id == memberId && GroupMatchesSearch(grp, searchQueryLower))
                                categoryHasMatch = true;

                //_ Same search-skip behavior as Basic Events above.
                bool catOpen = false;
                if (!searchActive || categoryHasMatch)
                {
                    if (searchActive)
                        ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

                    std::string oldCategoryName = DisplayName(cat, CategoryListKind::Cyclic);
                    bool categoryAutoFocus = (s_pendingCyclicCategoryFocus == c);
                    if (categoryAutoFocus)
                    {
                        editingCyclicCategoryNames[c] = ""; //. freshly created - starts empty, forces the inline editor open
                        s_pendingCyclicCategoryFocus = -1;
                    }
                    bool categoryIsNew = (s_newCyclicCategoryIndex == c);
                    NameRowResult nameResult = DrawNameAndContextMenu("##cyclic_category_node", c, c, oldCategoryName, editingCyclicCategoryNames, pendingRemoveCyclicCategoryIndex,
                        nullptr, std::string(), nullptr, nullptr, -1, nullptr, nullptr, true, categoryAutoFocus, categoryIsNew);
                    catOpen = nameResult.open;
                    MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, c);
                    if (nameResult.newName != oldCategoryName)
                        cat.customName = nameResult.newName;
                    //_ Resolved (saved or cancelled) - frees the "+" button back up.
                    if (categoryIsNew && (nameResult.cancelled || nameResult.newName != oldCategoryName))
                        s_newCyclicCategoryIndex = -1;
                }

                //_ Same unconditional-bookkeeping/gated-draw split as Basic Events above.
                for (const std::string& memberId : cat.members)
                {
                    for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
                    {
                        if (g_CyclicGroups[i].id != memberId) continue;
                        isGroupCategorized[i] = true;

                        bool memberMatches = categoryNameMatches || GroupMatchesSearch(g_CyclicGroups[i], searchQueryLower);

                        if (catOpen && memberMatches)
                        {
                            ImGui::PushID(i);
                            DrawCyclicGroupRow(i, pendingRemoveGroupIndex);
                            ImGui::PopID();
                        }
                        break;
                    }
                }

                if (catOpen)
                {
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
            {
                if (isGroupCategorized[i]) continue;
                if (!GroupMatchesSearch(g_CyclicGroups[i], searchQueryLower)) continue;

                ImGui::PushID(i);
                DrawCyclicGroupRow(i, pendingRemoveGroupIndex);
                ImGui::PopID();
            }

            if (pendingRemoveGroupIndex >= 0)
                g_CyclicGroups.erase(g_CyclicGroups.begin() + pendingRemoveGroupIndex);

            if (pendingAddGroup)
            {
                std::unordered_set<std::string> usedIds;
                for (const auto& g : g_CyclicGroups) usedIds.insert(g.id);

                CyclicGroup newGroup{};
                //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
                newGroup.id         = UniqueId(SlugifyName("group"), usedIds);
                newGroup.customName = ""; //. starts unnamed - forces the inline editor open on next draw (RequestCyclicGroupNameEdit below)
                newGroup.continentX = 49332.0f;
                newGroup.continentY = 31457.0f;
                newGroup.period     = 7200; //. 2h, most common period
                newGroup.colors     = ColorSet{ ImVec4(0.502f, 0.502f, 0.502f, 1.0f) }; //. neutral gray, placeholder
                g_CyclicGroups.push_back(newGroup);
                RequestCyclicGroupNameEdit((int)g_CyclicGroups.size() - 1);
            }

            if (pendingRemoveCyclicCategoryIndex >= 0)
                g_CyclicCategories.erase(g_CyclicCategories.begin() + pendingRemoveCyclicCategoryIndex);

            if (pendingAddCyclicCategory)
            {
                std::unordered_set<std::string> usedIds;
                for (const auto& c : g_CyclicCategories) usedIds.insert(c.id);

                Category newCat;
                //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
                newCat.id         = UniqueId(SlugifyName("category"), usedIds);
                newCat.customName = ""; //. starts unnamed - forces the inline editor open on next draw
                g_CyclicCategories.push_back(newCat);
                s_pendingCyclicCategoryFocus = (int)g_CyclicCategories.size() - 1;
                s_newCyclicCategoryIndex     = s_pendingCyclicCategoryFocus;
            }

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(Tr("WE_OPT_LIVE_EVENTS_HEADER")))
    {
        //_ Informational panel above the controls - explains the feature before the checkboxes, not a control itself.
        static const ImVec4 kInfoHeaderColor(0.65f, 0.80f, 1.00f, 1.0f);

        ImGui::TextColored(kInfoHeaderColor, "%s", Tr("WE_OPT_LIVE_HOW_IT_WORKS_HEADING"));
        ImGui::TextWrapped("%s", Tr("WE_OPT_LIVE_HOW_IT_WORKS_BODY"));
        ImGui::Spacing();

        ImGui::TextColored(kInfoHeaderColor, "%s", Tr("WE_OPT_LIVE_WHAT_DATA_HEADING"));
        ImGui::TextWrapped("%s", Tr("WE_OPT_LIVE_WHAT_DATA_BODY"));
        ImGui::Spacing();

        ImGui::TextColored(kInfoHeaderColor, "%s", Tr("WE_OPT_LIVE_WHERE_HEADING"));
        ImGui::TextWrapped("%s", Tr("WE_OPT_LIVE_WHERE_BODY"));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox(Tr("WE_OPT_LIVE_SUBSCRIBE_CHECKBOX"), &LiveEventsSubscribed);
        Tooltip(Tr("WE_OPT_LIVE_SUBSCRIBE_TIP"));

        ImGui::SameLine();
        if (ImGui::SmallButton(Tr("WE_OPT_LIVE_DEBUG_WS_BUTTON")))
            ShowWsDebugWindow = true;
        Tooltip(Tr("WE_OPT_LIVE_DEBUG_WS_TIP"));

        if (Gw2ApiKey.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", Tr("WE_OPT_LIVE_NO_API_KEY_WARNING"));
        }
        else if (GetLiveEventsRegion() == LiveEventsRegion::Unknown)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", Tr("WE_OPT_LIVE_REGION_UNKNOWN_WARNING"));
        }

        //_ RGB only (feeds the toast's accent stripe via ToImVec4), same convention as the Active/Soon pickers above.
        ImGui::ColorEdit3(TrId("WE_LIVE_REPORT_COLOR", "##sub_color_live").c_str(), SubscriptionsLiveColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        Tooltip(Tr("WE_LIVE_REPORT_COLOR_TIP"));

        ImGui::Checkbox(Tr("WE_LIVE_SHARE_NAME_REPORTS"), &ShareNameInReports);
        Tooltip(Tr("WE_LIVE_SHARE_NAME_REPORTS_TIP"));

        ImGui::Checkbox(Tr("WE_OPT_LIVE_MOVE_BUTTON"), &LiveEventButtonMoveMode);
        Tooltip(Tr("WE_OPT_LIVE_MOVE_BUTTON_TIP"));

        ImGui::Checkbox(Tr("WE_OPT_LIVE_SHOW_REPORTS_WINDOW"), &ShowLiveEventReportsWindow);
        Tooltip(Tr("WE_OPT_LIVE_SHOW_REPORTS_WINDOW_TIP"));

        DisabledBlock(!ShowLiveEventReportsWindow)
        {
            ImGui::Checkbox(Tr("WE_OPT_LIVE_LOCK_WINDOW"), &LiveEventReportsWindowLocked);
            Tooltip(Tr("WE_OPT_LIVE_LOCK_WINDOW_TIP"));
        }

        ImGui::Checkbox(Tr("WE_OPT_LIVE_SHOW_MAP_DOTS"), &ShowLiveEventMapDots);
        Tooltip(Tr("WE_OPT_LIVE_SHOW_MAP_DOTS_TIP"));
        ImGui::Spacing();

        ImGui::TextDisabled("%s", Tr("WE_OPT_LIVE_ROSTER_NOTE"));
        ImGui::Spacing();

        if (g_LiveEvents.empty())
        {
            ImGui::TextDisabled("%s", Tr("WE_LIVE_NONE_COMPILED"));
        }
        else
        {
            for (const LiveEvent& ev : g_LiveEvents)
            {
                ImGui::BulletText("%s", DisplayName(ev));
                ImGui::SameLine();
                ImGui::TextDisabled("(map %d)", ev.mapId);
            }
        }
    }
}