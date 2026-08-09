//################################################################################
// addon_options.cpp
//--------------------------------------------------------------------------------
// AddonOptions()   draws the World Events section of the Nexus options panel
//--------------------------------------------------------------------------------
// Nexus UI callback - draws into a panel Nexus owns, not a standalone window.
// Widgets write directly into the global settings (settings.h /
// settings_table.h) or into g_Events / g_CyclicGroups / g_BasicCategories /
// g_CyclicCategories. There is no explicit "Save" button: everything is
// written to disk on AddonUnload (see addon.cpp), so edits here just live
// in memory until the addon (or the game) closes.
//
// Covers both the flat scalar settings (overlay visibility, ring radius/
// thickness, entry/exit window) and full editing of individual events,
// cyclic groups/slots, and categories - creating, renaming, deleting,
// recoloring, drag-and-drop categorization, and icon assignment.
//
// The widget-drawing helpers themselves (scoped-disable, period widget,
// icon/color pickers, duplicate-name checks, drag-and-drop plumbing, the
// notify-level control, the shared name/context-menu row, search
// predicates, and the two full row drawers) live in
// addon_options_helpers.h/.cpp - this file is just AddonOptions() itself,
// assembling those pieces into the panel layout.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "addon_options_helpers.h"
#include "build_info.h"
#include "events.h"
#include "events_categories.h"
#include "events_tracking.h"
#include "gw2_api.h"
#include "icon_whitener.h"
#include "imgui.h"
#include "notify_sound.h"
#include "reset_defaults.h"
#include "settings.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonOptions
//--------------------------------------------------------------------------------
// Laid out as three stacked BeginTable/EndTable pairs plus two full-width
// CollapsingHeaders (one wrapping Table 2+3, one nested around just the
// search box and Table 3) - a CollapsingHeader clips to a single table
// column, so it can't be drawn inside either table. List
// mutations (add/remove event, group, category) are captured as bools
// during the row loop and applied afterward, to avoid invalidating
// indices mid-iteration. One search box filters both the Basic and
// Cyclic trees at once.
//--------------------------------------------------------------------------------
void AddonOptions()
{
    OptionsRenderTimer optionsRenderTimer; //. no-op unless ShowDebug
    ImVec2 dummySquare = ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight());
    
    ImGui::Text("World Events");
    ImGui::SameLine();
    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    ImGui::SameLine();
    if constexpr (ShowDebug)
    {
        //_ "Render" is AddonRender cost alone (map rings/bar/window/
        // notifications); "Options UI" is this panel's own per-frame cost.
        ImGui::TextDisabled("Render: %.3f ms avg (1s)", g_AvgRenderTimeMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Options UI: %.3f ms avg (1s)", g_AvgOptionsRenderTimeMs);

        //_ Per-view Data (cache refresh/adaptation) vs Draw (actual pixel
        // work) split, so "why is view X slow" maps to one number.
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
    if (ImGui::Checkbox("Disable overlay in PvP/WvW", &disableAllCompetitive))
    {
        DisableWindowWhenCompetitive = disableAllCompetitive;
        DisableBarWhenCompetitive    = disableAllCompetitive;
        DisableNotifyWhenCompetitive = disableAllCompetitive;
    }
    Tooltip("Hides map events, cyclic rings, and all subscriptions\n"
            "views (window/bar/toast) while you're on a PvP or WvW\n"
            "map. Doesn't change what's subscribed, only what shows.");

    ImGui::SameLine();
    ImGui::Checkbox("Window##dis_comp_window", &DisableWindowWhenCompetitive);
    ImGui::SameLine();
    ImGui::Checkbox("Toast##dis_comp_toast", &DisableNotifyWhenCompetitive);
    ImGui::SameLine();
    ImGui::Checkbox("Bar##dis_comp_bar", &DisableBarWhenCompetitive);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    //_ Table 1 - Subscriptions, always visible above the collapsing header.
    // Split out because CollapsingHeader can't span table columns.
    if (ImGui::BeginTable("##subs_table", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextRow();

        //_ Column 0: Subscriptions window, then Notification popups
        ImGui::TableSetColumnIndex(0);

        //_ Watchlist window toggle only opens/closes the window; it doesn't
        // affect which events are subscribed (that's events.json data).
        ImGui::Checkbox("Show subscriptions window", &ShowSubscriptionsWindow);
        DisabledBlock(!ShowSubscriptionsWindow)
        {
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::Checkbox("Hide active in window", &SubscriptionsHideActive);
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();

            //_ RGB only (feeds ImGui::TextColored, not a tinted dot/icon
            // like BasicEventColor* below, which need an alpha bar too).
            ImGui::ColorEdit3("Active##sub_color_active", SubscriptionsActiveColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

            ImGui::SameLine();
            ImGui::ColorEdit3("Soon##sub_color_soon", SubscriptionsSoonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        }

        ImGui::Dummy(dummySquare);

        //_ Third, independent view of the same subscription data (toast
        // popups); not gated by window/bar visibility, so it can stand alone.
        ImGui::Checkbox("Enable notification popups", &NotificationsEnabled);
        Tooltip("Pops up a small toast in the lower-right corner for events\n"
                "you have notifications enabled for, whether or not the\n"
                "window or distribution line are open. Click a popup to paste\n"
                "its waypoint code, same as clicking a row/segment there.");

        DisabledBlock(!NotificationsEnabled)
        {
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::InputInt("Warn before start (min)", &NotificationLeadMinutes, 0, 0))
            {
                //_ 0 is a valid value ("off"); floor is 0, not 1.
                if (NotificationLeadMinutes < 0)   NotificationLeadMinutes = 0;
                if (NotificationLeadMinutes > 120) NotificationLeadMinutes = 120;
            }
            Tooltip("How long before a subscribed event/slot starts to\n"
                    "fire the \"starting soon\" popup. 0 disables it.");
                
            ImGui::SameLine();
            ImGui::Checkbox("Notify on start", &NotificationOnStart);
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::InputInt("Popup duration (sec)", &NotificationDisplaySeconds, 0, 0))
            {
                if (NotificationDisplaySeconds < 1)   NotificationDisplaySeconds = 1;
                if (NotificationDisplaySeconds > 120) NotificationDisplaySeconds = 120;
            }
            Tooltip("How long a popup stays fully visible before it fades out.\n"
                    "Hovering a popup pauses its timer.");

            //_ Single .wav file, picked from "<addon dir>/sounds"; which
            // events actually play it is each row's own notify level (3).
            {
                const std::vector<std::string>& soundFiles = GetNotificationSoundFilenames();

                //_ Reuses DrawSpeakerIcon (notify level 3's icon) as a
                // marker that this row is about the notification sound.
                {
                    float sq = ImGui::GetFrameHeight();
                    ImVec2 rmin = ImGui::GetCursorScreenPos();
                    ImVec2 center(rmin.x + sq * 0.5f, rmin.y + sq * 0.5f);
                    DrawSpeakerIcon(ImGui::GetWindowDrawList(), center, sq * 0.96f, ImGui::GetColorU32(ImGuiCol_Text));
                    ImGui::Dummy(ImVec2(sq, sq));
                }
                ImGui::SameLine();

                std::vector<const char*> soundLabels;
                soundLabels.push_back("(none)");
                for (const auto& fn : soundFiles)
                    soundLabels.push_back(fn.c_str());

                int soundIndex = 0; //. "(none)"
                if (!NotificationSoundFile.empty())
                    for (int k = 0; k < (int)soundFiles.size(); k++)
                        if (soundFiles[k] == NotificationSoundFile) { soundIndex = k + 1; break; }

                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::Combo("Sound", &soundIndex, soundLabels.data(), (int)soundLabels.size()))
                    NotificationSoundFile = (soundIndex == 0) ? std::string() : soundFiles[soundIndex - 1];

                ImGui::SameLine();
                ImGui::TextDisabled("(.wav)");
                ImGui::SameLine();
                if (ImGui::Button("Rescan"))
                    ScanNotificationSoundFiles();
                Tooltip("Re-scans \"<addon dir>/sounds\" for .wav files you've\n"
                        "dropped in since the dropdown was last built.");
                        
                ImGui::SameLine();
                DisabledBlock(NotificationSoundFile.empty())
                {
                    if (ImGui::Button("Test"))
                        PlayNotificationSound(NotificationSoundFile);
                }
                Tooltip("Drop .wav files into \"<addon dir>/sounds\" and pick one\n"
                        "here to preview it. Only .wav is supported (PlaySound has\n"
                        "no built-in decoder for mp3/ogg/etc). \"Test\" just plays it\n"
                        "immediately — it also plays automatically alongside a real\n"
                        "notification popup, but only for events/slots whose own\n"
                        "notify level has sound enabled (see the speaker icon on\n"
                        "each row below).");
            }
        }

        //_ Column 1: Subscriptions bar
        ImGui::TableSetColumnIndex(1);

        //_ Second, alternate view of the subscription data: a thin animated
        // line pinned to the screen edge, not a window (no titlebar).
        ImGui::Checkbox("Show subscriptions bar", &ShowSubscriptionsBar);

        DisabledBlock(!ShowSubscriptionsBar)
        {
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::Checkbox("Hide active on bar", &SubscriptionsBarHideActive);
            Tooltip("Segments that are currently active are left off the bar entirely\n"
                    "instead of showing as a dropped-to-startX line — only upcoming events\n"
                    "are shown. Independent from \"Hide active in window\" above.");

            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::Checkbox("Minimal Mode", &SubscriptionsBarMinimalMode);
            ImGui::SameLine();
            ImGui::Checkbox("Bottom Line", &SubscriptionsBarBottomAnchored);
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::ColorEdit4("Dot Color##bar_dot_color", SubscriptionsBarDotColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::InputInt("Pop-out height (px)", &SubscriptionsBarMaxDropPx, 0, 0))
            {
                //_ Floored at 8, not 0: subscriptions_bar.cpp derives the
                // pill's corner radius from half this value.
                if (SubscriptionsBarMaxDropPx < 8)     SubscriptionsBarMaxDropPx = 8;
                if (SubscriptionsBarMaxDropPx > 300)   SubscriptionsBarMaxDropPx = 300;
            }
            Tooltip("How tall the dropped block/pill is, in px.\n"
                    "Sized by default to fit two centered lines of label text;\n"
                    "raise it if your font/DPI needs more room.");
                
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::InputInt("Pop-out delay (ms)", &SubscriptionsBarHoverDelayMs, 0, 0))
            {
                //_ Clamp after the fact rather than reject, since InputInt
                // allows transient out-of-range input. 0 is valid (disabled).
                if (SubscriptionsBarHoverDelayMs < 0)    SubscriptionsBarHoverDelayMs = 0;
                if (SubscriptionsBarHoverDelayMs > 5000) SubscriptionsBarHoverDelayMs = 5000;
            }
            Tooltip("How long the mouse has to sit still over a segment or dot\n"
                    "before it pops out. 0 = instant.");

            float screenWidth = ImGui::GetIO().DisplaySize.x;
            float screenHeight = ImGui::GetIO().DisplaySize.y;

            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::Text("Unsafe zone");
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt("Left##leftuz", &SubscriptionsBarUnsafeLeftPx, 1, 0,0, "%dpx"))
            {
                if (SubscriptionsBarUnsafeLeftPx < 0)           SubscriptionsBarUnsafeLeftPx = 0;
                if (SubscriptionsBarUnsafeLeftPx > screenWidth)  SubscriptionsBarUnsafeLeftPx = (int)screenWidth;
                if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                    SubscriptionsBarUnsafeRightPx = (int)screenWidth - SubscriptionsBarUnsafeLeftPx;
            }
            bool leftActive = ImGui::IsItemActive();
            Tooltip("Width from the LEFT screen edge, in px, treated as\n"
                    "covered by your own GW2 UI (e.g. party/buffs). Segments\n"
                    "in this zone drop lower instead of covering it.\n"
                    "0 disables the left zone.");
            
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt("Right##rightuz", &SubscriptionsBarUnsafeRightPx, 1, 0, 0, "%dpx"))
            {
                if (SubscriptionsBarUnsafeRightPx < 0)           SubscriptionsBarUnsafeRightPx = 0;
                if (SubscriptionsBarUnsafeRightPx > screenWidth)  SubscriptionsBarUnsafeRightPx = (int)screenWidth;
                if (SubscriptionsBarUnsafeLeftPx + SubscriptionsBarUnsafeRightPx > screenWidth)
                    SubscriptionsBarUnsafeLeftPx = (int)screenWidth - SubscriptionsBarUnsafeRightPx;
            }
            bool rightActive = ImGui::IsItemActive();
            Tooltip("Width from the RIGHT screen edge, in px, treated as\n"
                    "covered by your own GW2 UI (e.g. minimap/compass).\n"
                    "Segments in this zone drop lower instead of covering it.\n"
                    "0 disables the right zone.");
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::DragInt("Height##heightuz", &SubscriptionsBarUnsafeHeightPx, 1, 0, 0, "%dpx"))
            {
                if (SubscriptionsBarUnsafeHeightPx < 0)    SubscriptionsBarUnsafeHeightPx = 0;
                if (SubscriptionsBarUnsafeHeightPx > screenHeight) SubscriptionsBarUnsafeHeightPx = screenHeight;
            }
            bool heightActive = ImGui::IsItemActive();
            Tooltip("How tall your corner UI is, in px. Segments inside\n"
                    "either unsafe zone start their drop this far down\n"
                    "instead of from the line itself, so the popped-out\n"
                    "block clears your UI.");
            
            //_ Live preview, shown only while one of the three fields above
            // is focused; mirrors subscriptions_bar.cpp's own anchor math.
            if (leftActive || rightActive || heightActive)
            {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                const ImU32 kYellow = IM_COL32(255, 220, 0, 255);
                const float kDropDir  = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
                const float kBaselineY = SubscriptionsBarBottomAnchored
                    ? (ImGui::GetIO().DisplaySize.y - 1.0f)
                    : 1.0f;
                const float h = kBaselineY + kDropDir * (float)SubscriptionsBarUnsafeHeightPx;
            
                //_ Left zone: vertical edge + horizontal top from the screen edge to it
                dl->AddLine(ImVec2((float)SubscriptionsBarUnsafeLeftPx, kBaselineY),
                            ImVec2((float)SubscriptionsBarUnsafeLeftPx, h), kYellow, 2.0f);
                dl->AddLine(ImVec2(0.0f, h),
                            ImVec2((float)SubscriptionsBarUnsafeLeftPx, h), kYellow, 2.0f);
            
                //_ Right zone: mirrored
                float xRight = screenWidth - (float)SubscriptionsBarUnsafeRightPx;
                dl->AddLine(ImVec2(xRight, kBaselineY), ImVec2(xRight, h), kYellow, 2.0f);
                dl->AddLine(ImVec2(xRight, h), ImVec2(screenWidth, h), kYellow, 2.0f);
            }
        }

        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Events Settings (Basic|Cyclic)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        //_ Table 2 - Search/API key (Row 1) and section controls (Row 2);
        // exists only while the header above is expanded.
        if (ImGui::BeginTable("##world_events_table", 2, ImGuiTableFlags_SizingStretchSame))
        {
            //_ Row 1 - Search/Paste (col 0), API key/tracking (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("Chat settings:");
            static bool unlockDelay = false;
            ImGui::Checkbox("##lock_delay", &unlockDelay);
            Tooltip("Best to only change this if you have any issues.\n"
                    "Defines the internal delay set to properly paste text to chatbox.\n"
                    "Default = 20ms");
            ImGui::SameLine();
            DisabledBlock(!unlockDelay)
            {
                ImGui::SetNextItemWidth(50.0f);
                ImGui::InputInt("Paste Delay", &delayMilliseconds, 0 , 0);
            }
            
            {
                //_ Label/prefix pair, indexed together; index 0 stores an
                // empty prefix (ChatChannelPrefix), i.e. "current chat".
                static const char* const kChatChannelLabels[] = {
                    "Current chat (default)", "Say", "Party", "Squad",
                    "Guild (represented)", "Guild 1", "Guild 2", "Guild 3",
                    "Guild 4", "Guild 5", "Map"
                };
                static const char* const kChatChannelPrefixes[] = {
                    "", "/s ", "/p ", "/d ",
                    "/g ", "/g1 ", "/g2 ", "/g3 ",
                    "/g4 ", "/g5 ", "/m "
                };
                constexpr int kChatChannelCount = sizeof(kChatChannelLabels) / sizeof(kChatChannelLabels[0]);

                int chatChannelIndex = 0;
                for (int ci = 0; ci < kChatChannelCount; ci++)
                {
                    if (ChatChannelPrefix == kChatChannelPrefixes[ci]) { chatChannelIndex = ci; break; }
                }

                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::Combo("Paste to", &chatChannelIndex, kChatChannelLabels, kChatChannelCount))
                    ChatChannelPrefix = kChatChannelPrefixes[chatChannelIndex];

                Tooltip("Which chat channel a watchlist row/segment/toast click\n"
                        "pastes into, regardless of whatever channel is currently\n"
                        "selected in-game. Prepends that channel's slash command\n"
                        "(e.g. \"/p \") before the name/waypoint. \"Current chat\"\n"
                        "pastes exactly as before, into whichever channel already\n"
                        "has focus.");
            }
            
            ImGui::Dummy(dummySquare);
            
            //_ Zoom-based marker scaling; disabled by default keeps the old
            // fixed-size behavior, this just makes it optional and tunable.
            {
                ImGui::Checkbox("Grow markers when zooming in##basic_zoom_scaling_enabled", &BasicEventZoomScalingEnabled);
    
                DisabledBlock(!BasicEventZoomScalingEnabled)
                {
                    ImGui::Dummy(dummySquare);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat("Start growing at##basic_zoom_start_pct", &BasicEventZoomStartPct, 1.0f, 0.0f, 100.0f, "%.0f%%");
                    ImGui::Dummy(dummySquare);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat("Max size at 100\% zoom##basic_zoom_max_mult", &BasicEventZoomMaxMultiplier, 1.0f, 1.0f, 4.0f, "%.1fx");
                }
            }

            ImGui::TableSetColumnIndex(1);
            //_ Not gated by window/bar/notifications visibility: this key
            // drives auto-hiding completed content from all three views.
            ImGui::TextUnformatted("GW2 API key");
            ImGui::SameLine();
            ImGui::TextDisabled("Can take up to 5min to take effect.");

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
            Tooltip("Needs the \"progression\" permission. When set, a subscribed\n"
                    "Core Boss (Admiral Taidha Covington, Tequatl, etc.) is\n"
                    "automatically left off the watchlist window and bar once\n"
                    "your account has already killed it since the last daily\n"
                    "reset. The same applies to any subscribed slot in Verdant\n"
                    "Brink, Auric Basin, Tangled Depths, Dragon's Stand, Crystal\n"
                    "Oasis, Elon Riverlands, The Desolation, or Domain of Vabbi,\n"
                    "once that map's Hero's Choice Chest has already been\n"
                    "claimed today (the whole ring hides together, not just the\n"
                    "one slot). Nothing else is affected: the public API has no\n"
                    "\"already done today\" signal for any other event type in\n"
                    "this addon (other map metas, invasions, LLA, convergences),\n"
                    "so those are never hidden by this.");

            ImGui::SameLine();
            switch (GetGw2ApiStatus())
            {
                case Gw2ApiStatus::NoKey:
                    ImGui::TextDisabled("No key set");
                    break;
                case Gw2ApiStatus::Pending:
                    ImGui::TextDisabled("Checking...");
                    break;
                case Gw2ApiStatus::Ok:
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Connected");
                    break;
                case Gw2ApiStatus::InvalidKey:
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid key / missing permission");
                    break;
                case Gw2ApiStatus::NetworkError:
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Network error, retrying");
                    break;
            }

            //_ Master switch: drives whether any of the three subscription
            // views auto-surfaces this week's Wizard's Vault targets.
            ImGui::Checkbox("Auto-track weekly Wizard's Vault targets", &WeeklyAutoTrackEnabled);
            Tooltip("When on (default), the subscriptions window, distribution\n"
                    "line, and notification popups all automatically surface any\n"
                    "Basic Event / Cyclic slot that's an active-and-incomplete\n"
                    "target of this week's Wizard's Vault rotation, even if you\n"
                    "never subscribed to it yourself, marked with a small red\n"
                    "dot/border. Turn this off to see only what you've actually\n"
                    "subscribed to by hand in all three views. Doesn't affect\n"
                    "the red marker on something you HAVE manually subscribed to\n"
                    "that also happens to be a weekly target — that stays either way.");
            
            //_ Color swatch for the weekly Wizard's Vault tracked dot
            DisabledBlock(!WeeklyAutoTrackEnabled)
            {
                ImGui::ColorEdit4("Weekly Color##weekly_tracking_color", WeeklyAutoTrackColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }
            
            ImGui::Dummy(dummySquare);

            //_ Manual counterpart to the API-based hiding above; covers
            // everything the API doesn't, with or without an API key set.
            static bool unlockMarkers = false;
            ImGui::Checkbox("##lock_markers", &unlockMarkers);
            Tooltip("Right-click any row in the watchlist window, segment on the\n"
                    "distribution line, or notification popup to mark it done for\n"
                    "today. It then hides from all three views, the same way an\n"
                    "API-confirmed Core Boss kill or map chest claim does, until\n"
                    "the next daily reset (00:00 UTC) — or until you clear it\n"
                    "below. Right-click the same row again to undo it before then.");
            ImGui::SameLine();
            DisabledBlock(!unlockMarkers)
            {
                if (ImGui::Button("Clear events manually marked done"))
                    ClearAllDoneMarkers();
            }
            //_ Row 2 - Basic Events controls (col 0), Cyclic Events controls (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            //_ Only affects upcoming Basic Events (active always show); not
            // offered for cyclic groups (see BasicEventTimeFilterEnabled).
            {
                int mins = BasicEventTimeFilterMinutes;
                int h    = mins / 60;
                int m    = mins % 60;
            
                char label[96];
                if (h > 0)
                    snprintf(label, sizeof(label), "%dh %02dm", h, m);
                else
                    snprintf(label, sizeof(label), "%dm", m);
            
                ImGui::Checkbox("Only show events starting in##basic_time_filter_enabled", &BasicEventTimeFilterEnabled);
            
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

            //_ One shared color set for every Basic Event (not per-event),
            // matching the active/soon/waiting dot and icon-tint states.
            {
                ImGui::ColorEdit4("Active##basic_color_active", BasicEventColorActive, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4("Soon##basic_color_soon", BasicEventColorSoon, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4("Waiting##basic_color_waiting", BasicEventColorWaiting, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }

            //_ Independent settings, not derived from one another - dot and
            // icon sizes can differ freely relative to each other.
            {
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Dot radius##basic_dot_radius", &BasicEventDotRadius, 1.0f, 2.0f, 30.0f, "%.0f px");

                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Icon size##basic_icon_size", &BasicEventIconSize, 1.0f, 2.0f, 40.0f, "%.0f px");
            }

            DrawIconWhitenerButton();   //. opens the Icon Whitener modal
            DrawIconWhitenerPopup();    //. renders modal, no-op if closed

            ImGui::TableSetColumnIndex(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Show cyclic events on map", &ShowCyclicOverlay);
            DisabledBlock(!ShowCyclicOverlay)
            {
                ImGui::TextUnformatted("Ring appearance");
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Radius", &CyclicRadius, 1.0f, 5.0f, 50.0f, "%.0f px");
                if ( CyclicRadius < CyclicThickness / 2 ) { CyclicThickness = CyclicRadius * 2; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Thickness", &CyclicThickness, 1.0f, 5.0f, 100.0f, "%.0f px");
                if ( CyclicThickness > CyclicRadius * 2 ) { CyclicRadius = CyclicThickness / 2; }

                ImGui::TextUnformatted("Entry / exit window");
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Future window", &CyclicMaxFutureDeg,1.0f, 0.0f, 360.0f, "%.0f deg");
                if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxPastDeg = 360 - CyclicMaxFutureDeg; }
                Tooltip("How far ahead an upcoming event starts fading into view.\n"
                        "Measured in degrees of the ring.");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Past window", &CyclicMaxPastDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
                if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxFutureDeg = 360 - CyclicMaxPastDeg; }
                Tooltip("How long a finished event lingers before fading out.\n"
                        "Measured in degrees of the ring.");
            }
            
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Event Lists (Basic|Cyclic)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        //_ Table 3 - Basic Events tree (col 0), Cyclic Events tree (col 1);
        // split out so the search box below can filter both outside any column.

        //_ Transient UI state (not persisted); filters both Basic and
        // Cyclic trees at once - event name only for Basic, group+slot names for Cyclic.
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Search##global_search", searchBuf, sizeof(searchBuf));
        std::string searchQueryLower = searchBuf;
        std::transform(searchQueryLower.begin(), searchQueryLower.end(), searchQueryLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        bool searchActive = !searchQueryLower.empty();

        ImGui::SameLine();
        DrawResetToDefaultsButton();
        DrawResetToDefaultsPopup(); //. no-op unless the confirm popup is open

        ImGui::SameLine();
        ImGui::TextDisabled("(right-click an entry below for more options)");

        if (ImGui::BeginTable("##world_events_data", 2, ImGuiTableFlags_SizingStretchSame))
        {
            //_ Row 3 - Basic Events tree (col 0), Cyclic Events tree (col 1)
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            //_ Basic Events header + add buttons; add/remove itself is
            // deferred to after the tree loop below to avoid mid-loop
            // index invalidation.
            ImGui::TextUnformatted("Basic Events");
            MakeDropTarget(kBasicEventDragType, g_BasicCategories, -1);
            ImGui::SameLine();
            bool pendingAdd = ImGui::SmallButton("+##add_basic_event");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted("Categories");
            ImGui::SameLine();
            bool pendingAddBasicCategory = ImGui::SmallButton("+##add_basic_category");
        
            //_ Section-level bulk icon picker, applies to every Basic Event
            // regardless of category; no per-category equivalent exists.
            {
                std::vector<int> allIndices(g_Events.size());
                for (int bi = 0; bi < (int)g_Events.size(); bi++) allIndices[bi] = bi;
                ImGui::SetNextItemWidth(100.0f);
                DrawBulkIconPicker("Set all icons##bulk_icon_all", allIndices);
            }

            int pendingRemoveIndex = -1;
            int pendingRemoveBasicCategoryIndex = -1;
            static std::map<int, std::string> editingBasicCategoryNames;

            std::vector<bool> isCategorized(g_Events.size(), false);

            //_ Category-aware draw order: each category's members draw
            // first (nested, in g_BasicCategories order), then leftover
            // events draw after as the uncategorized bucket.
            for (int c = 0; c < (int)g_BasicCategories.size(); c++)
            {
                Category& cat = g_BasicCategories[c];
                ImGui::PushID(1000000 + c); //. offset clear of event indices

                //_ Resolved once up front: reused by the bulk icon picker
                // and by the membership loop below instead of re-searching.
                std::vector<int> memberIndices;
                for (const std::string& memberName : cat.members)
                    for (int mi = 0; mi < (int)g_Events.size(); mi++)
                        if (g_Events[mi].name == memberName) { memberIndices.push_back(mi); break; }

                bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
                bool categoryHasMatch = categoryNameMatches;
                if (!categoryHasMatch)
                    for (int mi : memberIndices)
                        if (EventMatchesSearch(g_Events[mi], searchQueryLower))
                            categoryHasMatch = true;

                //_ Set before TreeNode draws (SetNextItemOpen must be called
                // first) - starts false, set below only when the node draws.
                bool catOpen = false;
                if (!searchActive || categoryHasMatch)
                {
                    if (searchActive)
                        ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

                    NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, cat.name, editingBasicCategoryNames, pendingRemoveBasicCategoryIndex);
                    catOpen = nameResult.open;
                    MakeDropTarget(kBasicEventDragType, g_BasicCategories, c);
                    if (nameResult.newName != cat.name)
                    {
                        //_ No rename-patching needed - members reference
                        // categories by name in the other direction.
                        cat.name = nameResult.newName;
                    }
                }

                //_ Bookkeeping (isCategorized) runs unconditionally even
                // when catOpen is false, so a folded category doesn't leak
                // its members into the uncategorized pass below.
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
                WorldEvent newEvent{};
                newEvent.name       = "New Event";
                newEvent.continentX = 49332.0f;
                newEvent.continentY = 31457.0f;
                newEvent.isVarying  = false;
                newEvent.duration   = 900;  //. 15 min, a reasonable default
                newEvent.period     = 7200; //. 2h, most common period
                newEvent.offset     = 0;
                g_Events.push_back(newEvent);
            }

            if (pendingRemoveBasicCategoryIndex >= 0)
                g_BasicCategories.erase(g_BasicCategories.begin() + pendingRemoveBasicCategoryIndex);

            if (pendingAddBasicCategory)
                g_BasicCategories.push_back({ "New Category", {} });

            ImGui::TableSetColumnIndex(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        
            //_ Cyclic Events header + add buttons; same deferred add/remove
            // pattern as Basic Events above.
            ImGui::TextUnformatted("Cyclic Events");
            MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, -1); //. drop here to uncategorize
            ImGui::SameLine();
            bool pendingAddGroup = ImGui::SmallButton("+##add_cyclic_group");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted("Categories");
            ImGui::SameLine();
            bool pendingAddCyclicCategory = ImGui::SmallButton("+##add_cyclic_category");

            int pendingRemoveGroupIndex = -1;
            int pendingRemoveCyclicCategoryIndex = -1;
            static std::map<int, std::string> editingCyclicCategoryNames;

            std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

            //_ Same category-aware draw order as Basic Events above.
            for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
            {
                Category& cat = g_CyclicCategories[c];
                ImGui::PushID(2000000 + c); //. offset clear of other indices

                bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
                bool categoryHasMatch = categoryNameMatches;
                if (!categoryHasMatch)
                    for (const std::string& memberName : cat.members)
                        for (const auto& grp : g_CyclicGroups)
                            if (grp.name == memberName && GroupMatchesSearch(grp, searchQueryLower))
                                categoryHasMatch = true;

                //_ Same search-skip behavior as Basic Events above.
                bool catOpen = false;
                if (!searchActive || categoryHasMatch)
                {
                    if (searchActive)
                        ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

                    NameRowResult nameResult = DrawNameAndContextMenu("##cyclic_category_node", c, c, cat.name, editingCyclicCategoryNames, pendingRemoveCyclicCategoryIndex);
                    catOpen = nameResult.open;
                    MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, c);
                    if (nameResult.newName != cat.name)
                        cat.name = nameResult.newName;
                }

                //_ Same unconditional-bookkeeping/gated-draw split as Basic
                // Events above.
                for (const std::string& memberName : cat.members)
                {
                    for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
                    {
                        if (g_CyclicGroups[i].name != memberName) continue;
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
                CyclicGroup newGroup{};
                newGroup.name       = "New Cycle";
                newGroup.continentX = 49332.0f;
                newGroup.continentY = 31457.0f;
                newGroup.period     = 7200; //. 2h, most common period
                newGroup.colors     = ColorSet{ ImVec4(0.502f, 0.502f, 0.502f, 1.0f) }; //. neutral gray, placeholder
                g_CyclicGroups.push_back(newGroup);
            }

            if (pendingRemoveCyclicCategoryIndex >= 0)
                g_CyclicCategories.erase(g_CyclicCategories.begin() + pendingRemoveCyclicCategoryIndex);

            if (pendingAddCyclicCategory)
                g_CyclicCategories.push_back({ "New Category", {} });

            ImGui::EndTable();
        }
    }
}