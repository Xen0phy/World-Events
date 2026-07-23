// addon_options.cpp
// Implements the World Events section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns, not
// a standalone window. All widgets write directly into the global settings
// declared in settings.h / settings_table.h, or into g_Events/
// g_CyclicGroups/g_BasicCategories/g_CyclicCategories directly. There is no
// explicit "Save" button: everything is written to disk on AddonUnload
// (see addon.cpp), so edits here just live in memory until the addon (or
// the game) closes.
//
// Covers both the flat scalar settings (overlay visibility, ring radius/
// thickness, entry/exit window) and full editing of individual events,
// cyclic groups/slots, and categories — creating, renaming, deleting,
// recoloring, drag-and-drop categorization, and icon assignment.
//
// All of the actual widget-drawing helpers (scoped-disable, period
// widget, icon/color pickers, duplicate-name checks, drag-and-drop
// plumbing, the notify-level control, the shared name/context-menu row,
// search predicates, and the two full row drawers) live in
// addon_options_helpers.h/.cpp — this file is just AddonOptions() itself,
// assembling those pieces into the panel layout.

#include "addon_options_helpers.h"
#include "addon.h"
#include "events_tracking.h"
#include "settings.h"
#include "build_info.h"
#include "events.h"
#include "events_categories.h"
#include "icon_whitener.h"
#include "notify_sound.h"
#include "gw2_api.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the World Events section inside the Nexus options panel.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    OptionsRenderTimer optionsRenderTimer; // no-op unless ShowDebug is true — see ScopedRenderTimer in addon.h
    ImVec2 dummySquare = ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight());
    
    ImGui::Text("World Events");
    ImGui::SameLine();
    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    ImGui::SameLine();
    if constexpr (ShowDebug)
    {
        // Two separate numbers on purpose: "Render" is AddonRender alone
        // (map rings + subscriptions bar/window/notifications) — the cost
        // that actually matters during normal play. "Options UI" is this
        // panel's own per-frame rebuild cost (tree, search filter, color
        // pickers, icon previews for every group/slot) and only applies
        // while this panel is open — it was previously invisible and
        // easy to mistake for render cost when eyeballing total frame
        // time with the panel open.
        ImGui::TextDisabled("Render: %.3f ms avg (1s)", g_AvgRenderTimeMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Options UI: %.3f ms avg (1s)", g_AvgOptionsRenderTimeMs);

        // Per-view breakdown: "Data" is RefreshSubscriptionsCache plus each
        // view's own light adaptation of the shared resolved list (see
        // subscriptions_cache.h); "Draw" is everything from there to actual
        // pixels — dot/hover layout and ImGui calls for the bar, the
        // ImGui window/rows for the watchlist, popup draw/fade/expire for
        // notifications. Lets a specific "why is view X still costing Y"
        // question be answered by which of the two numbers is high,
        // instead of only having Render above as one combined total for
        // all three views together.
        ImGui::TextDisabled("Bar: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsBarDataMs, g_AvgSubsBarDrawMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Window: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsWindowDataMs, g_AvgSubsWindowDrawMs);
        ImGui::TextDisabled("Notify: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsNotifyDataMs, g_AvgSubsNotifyDrawMs);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---------------------------------------------------------------------
    // Table 1 — Subscriptions (Row 0). Always visible, above the collapsing
    // header below. Two columns: left = Subscriptions window + Notification
    // popups, right = Subscriptions bar. Kept in its own table rather than
    // folded into Table 2 further down — a CollapsingHeader can't span
    // table columns (its hit-rect/draw are clipped to whichever single
    // column it's called from), so the full-width header has to be drawn
    // *between* two separate BeginTable/EndTable pairs, not inside one
    // continuous table.
    //
    // NOTE: each of the three groups below is wrapped in its own
    // DisabledBlock(!ShowSubscriptionsWindow) / (!NotificationsEnabled) /
    // (!ShowSubscriptionsBar) — dimmed and non-interactive whenever that
    // group's own section checkbox is off, independently of the other two
    // groups sharing this table.
    // ---------------------------------------------------------------------
    if (ImGui::BeginTable("##subs_table", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextRow();

        // ---------------------------------------------------------------
        // Column 0: Subscriptions window, then Notification popups
        // ---------------------------------------------------------------
        ImGui::TableSetColumnIndex(0);

        // Watchlist window toggle — mirrors the "Watch" checkboxes on each
        // event/slot row further down: this just opens/closes the window,
        // it doesn't affect which events are actually subscribed (that's
        // events.json data, see subscriptions.h, not this bool).
        ImGui::Checkbox("Show subscriptions window", &ShowSubscriptionsWindow);
        DisabledBlock(!ShowSubscriptionsWindow)
        {
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();
            ImGui::Checkbox("Hide active in window", &SubscriptionsHideActive);
            
            ImGui::Dummy(dummySquare);
            ImGui::SameLine();

            // RGB only, no alpha bar — these feed straight into
            // ImGui::TextColored (plain text, no separate opacity control),
            // unlike the map's BasicEventColor* pickers, which DO need
            // ColorEdit4/an alpha bar since they tint an actual drawn dot/icon.
            // See SubscriptionsActiveColor's comment in settings_table.h.
            // Both settings are float[4] globals — ColorEdit3 writes
            // straight into the first 3 components, no wrapper needed.
            ImGui::ColorEdit3("Active##sub_color_active", SubscriptionsActiveColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

            ImGui::SameLine();
            ImGui::ColorEdit3("Soon##sub_color_soon", SubscriptionsSoonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        }

        ImGui::Dummy(dummySquare);

        // Third, independent view of the same subscription data — small
        // lower-right toast popups (subscriptions_notification.h/.cpp) instead
        // of a persistent list/strip. Not gated by ShowSubscriptionsWindow/Bar:
        // a user may want popups without either persistent view open at all.
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
                // 0 is a valid, meaningful value ("off" — only the on-start
                // popup below still fires), so the floor is 0, not 1.
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

            // Single notification sound file, picked from "<addon dir>/sounds".
            // Same disk-scan-and-Combo shape as the icon pickers above (see
            // DrawBulkIconPicker / GetEventIconFilenames), just for .wav files
            // instead of images — see notify_sound.h.
            //
            // This is a single global file, not a per-event choice: which
            // events actually play it is controlled by each event/slot's own
            // notify level (level 3 — see DrawNotifyLevelIcon and
            // subscriptions_notification.cpp's SpawnPopup call sites), same
            // "one global setting, per-event opt-in" split as the toast popup
            // itself. "Test" here just plays it immediately, independent of
            // any subscription state.
            {
                const std::vector<std::string>& soundFiles = GetNotificationSoundFilenames();

                // Plain label glyph — same DrawSpeakerIcon that's now level
                // 3's icon in DrawNotifyLevelIcon's cycle, reused here as a
                // visual marker for "this row is about the notification
                // sound", same fixed-square-slot sizing as DrawNotifyLevelIcon
                // uses.
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

                int soundIndex = 0; // "(none)"
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

        // ---------------------------------------------------------------
        // Column 1: Subscriptions bar
        // ---------------------------------------------------------------
        ImGui::TableSetColumnIndex(1);

        // Second, alternate view of the same subscription data — a thin
        // animated line pinned to the top edge of the screen (not a window:
        // no title bar, can't be dragged/resized/closed with a titlebar X,
        // just this checkbox).
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
                // Floored at 8, not 0 — subscriptions_bar.cpp derives the
                // detached pill's corner radius from half this value
                // (pillRx = height/2 for a true stadium cap), so it needs a
                // sane positive minimum rather than 0 being a valid "off"
                // state the way the delay/unsafe-zone settings allow.
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
                // Clamp rather than reject — InputInt lets the user type/
                // arrow past either end transiently, so clamp after the
                // fact instead of blocking input. 0 is a valid, meaningful
                // value (delay disabled entirely), so the floor is 0, not 1.
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
            
            // Live preview, only while one of the three fields above is focused.
            // Mirrors subscriptions_bar.cpp's own top/bottom-anchor math so the
            // preview lines up with where the real unsafe zone will actually sit.
            if (leftActive || rightActive || heightActive)
            {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                const ImU32 kYellow = IM_COL32(255, 220, 0, 255);
                const float kDropDir  = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
                const float kBaselineY = SubscriptionsBarBottomAnchored
                    ? (ImGui::GetIO().DisplaySize.y - 1.0f)
                    : 1.0f;
                const float h = kBaselineY + kDropDir * (float)SubscriptionsBarUnsafeHeightPx;
            
                // Left zone: vertical edge + horizontal top from the screen edge to it
                dl->AddLine(ImVec2((float)SubscriptionsBarUnsafeLeftPx, kBaselineY),
                            ImVec2((float)SubscriptionsBarUnsafeLeftPx, h), kYellow, 2.0f);
                dl->AddLine(ImVec2(0.0f, h),
                            ImVec2((float)SubscriptionsBarUnsafeLeftPx, h), kYellow, 2.0f);
            
                // Right zone: mirrored
                float xRight = screenWidth - (float)SubscriptionsBarUnsafeRightPx;
                dl->AddLine(ImVec2(xRight, kBaselineY), ImVec2(xRight, h), kYellow, 2.0f);
                dl->AddLine(ImVec2(xRight, h), ImVec2(screenWidth, h), kYellow, 2.0f);
            }
        }

        ImGui::EndTable();
    }

    // ---------------------------------------------------------------------
    // Full-width collapsing header — drawn OUTSIDE any table's column
    // context (between Table 1 and Table 2), since CollapsingHeader is
    // clipped to whichever single column it's called from and can't span
    // both. Table 2 below is only built while this is open — nothing in
    // Table 2 is drawn (or exists) on a frame where it's collapsed.
    //
    // Defaults open so the Basic/Cyclic events editor is visible on first
    // load, same as before this header existed.
    // ---------------------------------------------------------------------

    if (ImGui::CollapsingHeader("World Events (Basic + Cyclic)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // ---------------------------------------------------------------
        // Table 2 — Search/API key (Row 1) and section controls (Row 2).
        // Same 2-column shape as Table 1 above; only exists while the
        // header above is expanded. Ends before the search box below —
        // see Table 3's own header comment for why the Basic/Cyclic trees
        // (Row 3) are a separate table rather than a third row in this one.
        // ---------------------------------------------------------------
        if (ImGui::BeginTable("##world_events_table", 2, ImGuiTableFlags_SizingStretchSame))
        {
            // -----------------------------------------------------------
            // Row 1 — Search/Paste (col 0), API key/tracking (col 1)
            // -----------------------------------------------------------
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
            
            // Which chat channel a watchlist row/segment/toast click pastes
            // into. Stored as the literal slash-command text itself
            // (ChatChannelPrefix, e.g. "/p ") — see settings_table.h — so this
            // Combo is the one place that owns the label<->command mapping;
            // BuildChatPasteMessage (subscriptions.cpp) just prepends whatever
            // it finds there. Index 0 ("Current chat") stores an empty prefix,
            // i.e. paste as before with no channel switch.
            {
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
            
            // Zoom-based scaling — markers (and cyclic group rings, see
            // cyclicrender.cpp) grow as the map is zoomed in. Disabled by default
            // behavior is "stay fixed size", matching the old hardcoded behavior;
            // this just makes it optional and tunable.
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
            // Not gated by ShowSubscriptionsWindow/ShowSubscriptionsBar/
            // NotificationsEnabled: this key drives auto-hiding an already-
            // completed Core Boss or map meta from BOTH views
            // (subscriptions_window.cpp / subscriptions_bar.cpp), so it
            // belongs to "Subscriptions" as a whole rather than to either
            // individual view's own controls.
            ImGui::TextUnformatted("GW2 API key");
            ImGui::SameLine();
            ImGui::TextDisabled("Can take up to 5min to take effect.");

            {
                static char apiKeyBuf[128] = "";
                static bool bufInitialized = false;
                if (!bufInitialized) // one-time seed from the loaded setting, same pattern as other InputText fields in this file
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

            // Master switch drives whether ANY of the three subscription
            // views auto-surfaces this week's Wizard's Vault targets on top
            // of the user's own manual subscriptions.
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
            
            // Color swatch for the weekly Wizard's Vault trcked dot
            DisabledBlock(!WeeklyAutoTrackEnabled)
            {
                ImGui::ColorEdit4("Weekly Color##weekly_tracking_color", WeeklyAutoTrackColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }
            
            ImGui::Dummy(dummySquare);

            // Manual counterpart to the API-based "already done today" hiding
            // above — see events_tracking.h. Covers everything the public API
            // doesn't (every event/slot other than the 13 Core Bosses and 8
            // HoT/PoF map chests), and works with or without an API key set.
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
            // -----------------------------------------------------------
            // Row 2 — Basic Events controls (col 0), Cyclic Events
            // controls (col 1)
            // -----------------------------------------------------------
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Time-window filter — only show upcoming Basic Events starting within
            // the next N minutes; active events always show. Deliberately NOT
            // offered for cyclic groups (see BasicEventTimeFilterEnabled in
            // settings_table.h).
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

            // Status colors — one shared set for every Basic Event (not
            // per-event), matching the dot's/icon-tint's three states: active,
            // soon (<15 min out), and waiting. There's no separate opacity
            // control beyond whatever alpha the picker itself lets the user
            // choose for each color.
            {
                ImGui::ColorEdit4("Active##basic_color_active", BasicEventColorActive, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4("Soon##basic_color_soon", BasicEventColorSoon, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

                ImGui::SameLine();
                ImGui::ColorEdit4("Waiting##basic_color_waiting", BasicEventColorWaiting, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            }

            // Size — independent settings, not derived from one another, so an
            // icon-using event and a plain-dot event can look completely
            // different sizes relative to each other if the user wants that.
            {
                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Dot radius##basic_dot_radius", &BasicEventDotRadius, 1.0f, 2.0f, 30.0f, "%.0f px");

                ImGui::SetNextItemWidth(50.0f);
                ImGui::DragFloat("Icon size##basic_icon_size", &BasicEventIconSize, 1.0f, 2.0f, 40.0f, "%.0f px");
            }

            DrawIconWhitenerButton();   // opens the Icon Whitener modal when clicked
            DrawIconWhitenerPopup();    // renders the modal every frame (no-op when closed)

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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---------------------------------------------------------------
        // Table 3 — Basic Events tree (Row 3, col 0), Cyclic Events tree
        // (Row 3, col 1). Split out into its own BeginTable/EndTable pair,
        // separate from Table 2 above, so the search box right below
        // (added later) can sit outside any table's column context and
        // filter both trees at once — same reasoning as the CollapsingHeader
        // needing to sit between Table 1 and Table 2 rather than inside
        // either one.
        // ---------------------------------------------------------------

        // Static, not a setting: pure transient UI state, not worth
        // persisting across sessions. One box filters BOTH Basic Events and
        // Cyclic Events at once (event name only for Basic; group name +
        // every slot name for Cyclic).
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Search##global_search", searchBuf, sizeof(searchBuf));
        std::string searchQueryLower = searchBuf;
        std::transform(searchQueryLower.begin(), searchQueryLower.end(), searchQueryLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        bool searchActive = !searchQueryLower.empty();

        if (ImGui::BeginTable("##world_events_data", 2, ImGuiTableFlags_SizingStretchSame))
        {
            // -----------------------------------------------------------
            // Row 3 — Basic Events tree (col 0), Cyclic Events tree (col 1)
            // -----------------------------------------------------------
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Basic Events (g_Events) header + add buttons. Add/remove for
            // the events themselves are DEFERRED to after the tree loop in
            // Row 3 below — iterator/index invalidation otherwise, since
            // erasing mid-loop would shift every later index. pendingAdd /
            // pendingAddBasicCategory are captured here (button clicks) and
            // consumed down in Row 3's column 0.
            ImGui::TextUnformatted("Basic Events");
            MakeDropTarget(kBasicEventDragType, g_BasicCategories, -1); // drop here to uncategorize
            ImGui::SameLine();
            bool pendingAdd = ImGui::SmallButton("+##add_basic_event");
    
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted("Categories");
            ImGui::SameLine();
            bool pendingAddBasicCategory = ImGui::SmallButton("+##add_basic_category");
            
            // Section-level bulk icon picker — applies to literally every Basic
            // Event regardless of category. There is deliberately no equivalent
            // per-category picker — only this section-wide one and the
            // individual per-event dropdown in DrawBasicEventRow exist.
            {
                std::vector<int> allIndices(g_Events.size());
                for (int bi = 0; bi < (int)g_Events.size(); bi++) allIndices[bi] = bi;
                ImGui::SetNextItemWidth(100.0f);
                DrawBulkIconPicker("Set all icons##bulk_icon_all", allIndices);
            }

            // Category-aware draw order: each category's members are drawn first
            // (nested under a foldable header for that category), in the order the
            // categories themselves are listed in g_BasicCategories; whatever's
            // left over (not a member of any category) is drawn afterward as the
            // implicit "uncategorized" bucket. An item is matched into its
            // category BY NAME — see events_categories.h — so a member name that no
            // longer corresponds to any g_Events entry (e.g. the event was
            // deleted) is simply skipped when drawing, with no special handling
            // needed; it just silently doesn't render anywhere until the category
            // itself is edited to remove that stale reference.
            //
            // Assigning an item INTO a category is drag-and-drop (see
            // MakeDropTarget/BeginDragDropSource below and the payload-type
            // comment above) — this pass covers creating, renaming, and deleting
            // categories themselves, plus drawing whatever membership already
            // exists (from drag-and-drop or a hand-edited events.json). Deleting
            // a category does NOT delete its members' underlying events —
            // members are references, not copies (see events_categories.h) — it
            // just dissolves the grouping, and those events fall back into the
            // uncategorized bucket on the next frame.
            int pendingRemoveIndex = -1;
            int pendingRemoveBasicCategoryIndex = -1;
            static std::map<int, std::string> editingBasicCategoryNames;

            std::vector<bool> isCategorized(g_Events.size(), false);

            for (int c = 0; c < (int)g_BasicCategories.size(); c++)
            {
                Category& cat = g_BasicCategories[c];
                ImGui::PushID(1000000 + c); // offset well clear of any real event index

                // Pre-check (before drawing the TreeNode) whether this category
                // contains at least one search match, so SetNextItemOpen can
                // force it expanded BEFORE the TreeNode call itself — ImGui needs
                // to know the open state before drawing the node, not after.
                // Also pre-check whether the CATEGORY's own name matches, since a
                // category whose name itself matches should show all its
                // members, not just ones that individually match too.
                // Resolve this category's members to actual g_Events indices
                // once, up front — used both for the bulk icon picker (which
                // needs the index list before the header even draws) and reused
                // by the membership-bookkeeping loop below instead of
                // re-searching by name a second time.
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

                // When a search is active and this category has no match at
                // all, skip drawing its header entirely, so a non-matching
                // category disappears the same way a non-matching event/group
                // already does in the uncategorized pass, rather than just
                // sitting there folded shut. catOpen is left false in this case
                // (TreeNode is simply never called), and the membership loop
                // below still runs unconditionally regardless of whether the
                // header drew.
                bool catOpen = false;
                if (!searchActive || categoryHasMatch)
                {
                    if (searchActive)
                        ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

                    NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, cat.name, editingBasicCategoryNames, pendingRemoveBasicCategoryIndex);
                    catOpen = nameResult.open;
                    MakeDropTarget(kBasicEventDragType, g_BasicCategories, c);
                    if (nameResult.newName != cat.name)
                        cat.name = nameResult.newName; // no rename-patching needed: nothing else references a CATEGORY by name (unlike events/groups, members point at THEM, not the reverse)
                }

                // Membership bookkeeping happens UNCONDITIONALLY, every frame,
                // regardless of catOpen — an item must stay excluded from the
                // uncategorized pass below even while its category is folded
                // shut, since "is this item categorized" and "is the category
                // currently expanded enough to draw it" are two separate
                // questions. Only the actual row DRAWING is gated on catOpen.
                //
                // Search filtering: a member is drawn if it matches the search
                // itself, OR if the category's own name matches (in which case
                // every member shows, not just individually-matching ones) — but
                // isCategorized[i] is set regardless of whether it's drawn, so a
                // member hidden by an active search still correctly stays out of
                // the uncategorized pass rather than incorrectly reappearing
                // there just because the search filtered it out of view here.
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
                newEvent.duration   = 900;  // 15 min, a reasonable default
                newEvent.period     = 7200; // 2h, the most common period in existing data
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
            
            // Cyclic Events (g_CyclicGroups) header + add buttons. Same
            // deferred add/remove pattern as Basic Events above —
            // pendingAddGroup / pendingAddCyclicCategory are captured here
            // and consumed down in Row 3's column 1.
            ImGui::TextUnformatted("Cyclic Events");
            MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, -1); // drop here to uncategorize
            ImGui::SameLine();
            bool pendingAddGroup = ImGui::SmallButton("+##add_cyclic_group");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextUnformatted("Categories");
            ImGui::SameLine();
            bool pendingAddCyclicCategory = ImGui::SmallButton("+##add_cyclic_category");

            // Same category-aware draw order as Basic Events above.
            int pendingRemoveGroupIndex = -1;
            int pendingRemoveCyclicCategoryIndex = -1;
            static std::map<int, std::string> editingCyclicCategoryNames;

            std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

            for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
            {
                Category& cat = g_CyclicCategories[c];
                ImGui::PushID(2000000 + c); // offset clear of both event indices and basic-category indices

                // Same pre-check as Basic Events above: figure out match state
                // BEFORE the TreeNode call, since SetNextItemOpen has to be
                // called before the node is drawn, not after.
                bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
                bool categoryHasMatch = categoryNameMatches;
                if (!categoryHasMatch)
                    for (const std::string& memberName : cat.members)
                        for (const auto& grp : g_CyclicGroups)
                            if (grp.name == memberName && GroupMatchesSearch(grp, searchQueryLower))
                                categoryHasMatch = true;

                // When a search is active and this category has no match at all,
                // skip drawing its header entirely rather than just force-collapsing
                // it, so an empty, irrelevant category doesn't clutter search results.
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

                // Membership bookkeeping runs unconditionally every frame; only
                // the row DRAWING is gated on catOpen, otherwise a folded
                // category silently leaks its members back into the
                // uncategorized list below. A member draws if it matches the
                // search OR the category's own name matches.
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
                newGroup.period     = 7200; // 2h, the most common period in existing data
                newGroup.colors     = ColorSet{ ImVec4(0.502f, 0.502f, 0.502f, 1.0f) }; // neutral gray (0x808080FF); user picks a real color next
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