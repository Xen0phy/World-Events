//################################################################################
// subscriptions_window.cpp
//--------------------------------------------------------------------------------
// DrawSubscriptionRow        draws one watchlist row (internal helper)
// RenderSubscriptionsWindow  draws the standalone "Subscriptions" window
//--------------------------------------------------------------------------------
// Draws the standalone watchlist window: a unified, sorted list of rows across
// both Basic Events and Cyclic slots, each row custom-drawn (not
// ImGui::Selectable) for per-frame cost - see DrawSubscriptionRow.
//
// SubscriptionsActiveColor / SoonColor / WeeklyAutoTrackColor's alpha component
// is ignored via ToImVec4Opaque (color_utils.h) wherever those colors are used
// below: they feed straight into ImGui::TextColored, which is plain text at a
// fixed opacity of 1.0, not a translucency-capable draw.
//--------------------------------------------------------------------------------

#include "addon.h" //. SubsWindowDataTimer/SubsWindowDrawTimer - see addon.h
#include "color_utils.h"
#include "events_tracking.h"
#include "imgui.h"
#include "localization.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_ui.h"
#include "subscriptions_cache.h"
#include "subscriptions_edit_window.h"
#include "time_format.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//_ Untranslated on purpose (see WE_SUBS_WINDOW_TITLE, localization_table.h) - the "###" drops everything before it from ImGui's ID hash, so this stays stable across languages while the title bar text translates.
static constexpr const char* kSubscriptionsWindowId = "###WorldEventsSubscriptions";

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

//_ 15-minute "soon" threshold, matching BasicEventColorSoon's map check (maprender.cpp).
static constexpr int kSoonThresholdSecs = 900;

//_ Most-recently-clicked row (by name) and its GetTickCount64() flash deadline.
static std::string   s_flashKey;
static unsigned long long s_flashUntil = 0;

static constexpr unsigned long long kFlashDurationMs = 350;   //. click-confirmation flash duration

//_ Row that took the last left-button mouse-down; release must land on it too.
static std::string s_leftPressedKey;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscriptionRow
//--------------------------------------------------------------------------------
// Draws one watchlist row: click copies "<name>: <chatCode>" to the clipboard (or
// just "<name>" with no chat code yet), flashing a brief highlight as
// confirmation. Rows are hand-drawn straight to the window's draw list instead of
// one ImGui::Selectable each - a Selectable's ID hashing and hover/click state
// machine, paid per row per frame, made this window cost roughly 2x the
// subscription bar (subscriptions_bar.cpp) for a visually simpler job. Left-click
// requires press and release on the same row (Selectable's own gesture); right-
// click (open the "mark done" popup) fires on mouse-down, matching the pre-
// rewrite IsItemClicked() behavior exactly.
//--------------------------------------------------------------------------------
static bool DrawSubscriptionRow(const std::string& name, const std::string& chatCode, bool active, int secs, bool isWeekly,
    bool isBasic, const std::string& basicId, const CyclicSubscriptionKey& cyclicKey)
{
    if (isWeekly)
    {
        //_ Weekly Vault marker (weekly_vault.h) - purely visual, no effect on clicks below.
        ImVec4 weeklyDotColor = ToImVec4Opaque(WeeklyAutoTrackColor);
        ImGui::TextColored(weeklyDotColor, "*");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("WE_TIP_WEEKLY_VAULT"));
        ImGui::SameLine(0.0f, 4.0f);
    }

    std::string statusSuffix;
    ImVec4 color;
    bool useColor = true;

    if (active)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), Tr("WE_SUBS_STATUS_ACTIVE_FMT"), FormatMinSec(secs).c_str());
        statusSuffix = buf;
        color = ToImVec4Opaque(SubscriptionsActiveColor);
    }
    else if (secs < kSoonThresholdSecs)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), Tr("WE_SUBS_STATUS_IN_FMT"), FormatMinSec(secs).c_str());
        statusSuffix = buf;
        color = ToImVec4Opaque(SubscriptionsSoonColor);
    }
    else
    {
        char buf[48];
        snprintf(buf, sizeof(buf), Tr("WE_SUBS_STATUS_IN_FMT"), FormatCountdown(secs).c_str());
        statusSuffix = buf;
        useColor = false;
    }

    std::string label = name + statusSuffix;

    //_ Row rect + hover test, computed before drawing anything - same order as hoveredIndices in subscriptions_bar.cpp.
    ImVec2 rowMin       = ImGui::GetCursorScreenPos();
    float  rowWidth     = ImGui::GetContentRegionAvail().x;
    //_ Matches Selectable's text-only sizing; GetFrameHeight() would grow every row and the window.
    float  rowHeight    = ImGui::GetTextLineHeight();
    ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);

    //_ IsWindowHovered skips a hover reading when a popup, including this row's own, sits on top.
    bool hovered = ImGui::IsWindowHovered()
        && ImGui::IsMouseHoveringRect(rowMin, rowMax);

    bool flashing = (s_flashKey == name) && (GetTickCount64() < s_flashUntil);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    //_ Flash confirmation takes priority over the plain hover highlight.
    if (flashing)
        dl->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 90));
    else if (hovered)
        dl->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    ImU32 textColor = useColor ? ImGui::GetColorU32(color) : ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 textPos(rowMin.x, rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    dl->AddText(textPos, textColor, label.c_str());

    //_ Reserves layout/scroll space without creating an interactive item.
    ImGui::Dummy(ImVec2(rowWidth, rowHeight));

    //_ Records which row a press started on; release only acts if it lands back on it.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s_leftPressedKey = name;
    if (hovered && s_leftPressedKey == name && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        std::string toCopy = BuildChatPasteMessage(name, chatCode);
        PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));

        s_flashKey   = name;
        s_flashUntil = GetTickCount64() + kFlashDurationMs;
    }

    //_ Keyed by name (like s_flashKey), unique per row without a separate numeric id.
    std::string popupId = "##we_done_popup_" + name;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup(popupId.c_str());

    //_ Runs every frame regardless of hover, so an open popup keeps rendering off-row.
    if (ImGui::BeginPopup(popupId.c_str()))
    {
        if (ImGui::Selectable(Tr("WE_SUBS_MARK_DONE_TODAY")))
        {
            if (isBasic) ToggleBasicEventDoneToday(basicId);
            else         ToggleCyclicSlotDoneToday(cyclicKey);
        }
        ImGui::Separator();
        if (ImGui::Selectable(Tr("WE_SUBS_EDIT_SUBSCRIPTIONS")))
            OpenEditSubscriptionsWindow(isBasic ? SubscriptionKind::Basic : SubscriptionKind::Cyclic, basicId, cyclicKey);
        ImGui::EndPopup();
    }

    return hovered;
}

//********************************************************************************
// Row
//--------------------------------------------------------------------------------
// name, chatCode, active, secs, isWeekly    display + timing state for the row
// isBasic, basicId, cyclicKey               which flavor this row is, for the
//                                           right-click "mark done" toggle
//--------------------------------------------------------------------------------
struct Row { std::string name; std::string chatCode; bool active; int secs; bool isWeekly;
             bool isBasic = true; std::string basicId; CyclicSubscriptionKey cyclicKey; };

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsWindow
//--------------------------------------------------------------------------------
// Sortable, unified list of rows across both Basic Events and Cyclic slots, so
// "what's coming up soonest" reads as one list instead of two sections the user
// has to visually merge themselves - active entries first, then soonest-upcoming,
// matching the sort already used for the per-group tooltip in cyclicrender.cpp.
// isBasic/basicId/cyclicKey identify each row for
// ToggleBasicEventDoneToday/ToggleCyclicSlotDoneToday - see the right-click "Mark
// done for today" menu in DrawSubscriptionRow.
//--------------------------------------------------------------------------------
void RenderSubscriptionsWindow()
{
    if (!ShowSubscriptionsWindow) return;

    time_t now = time(nullptr);
    std::vector<Row> rows;
    {
        //_ Scoped to data gathering only, split from the draw timer below (see subscriptions_bar.cpp).
        SubsWindowDataTimer dataTimer; //. no-op unless ShowDebug
        RefreshSubscriptionsCache(now); //. no-op most frames

        //_ Shared cache, built once per RefreshSubscriptionsCache() call, reused by the bar/notifications too.
        const auto& resolved = GetResolvedSubscriptions();
        rows.reserve(resolved.size());

        for (const auto& sub : resolved)
        {
            if (sub.doneToday) continue; //. API-confirmed or manually marked

            SubscriptionActiveState as = GetSubscriptionActiveState(sub, now);
            int secs = as.active ? as.secsUntilEnd : as.secsUntilStart;
            if (secs < 0) continue; //. no timer data yet
            if (as.active && SubscriptionsHideActive) continue; //. hides already-active subscriptions

            rows.push_back({ sub.label, sub.chatCode, as.active, secs, sub.isWeeklyTarget,
                              sub.isBasic, sub.basicId, CyclicSubscriptionKey{ sub.cyclicGroupId, sub.cyclicSlotId } });
        }

        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
        {
            if (a.active != b.active) return a.active; //. active first
            return a.secs < b.secs;                    //. then soonest first
        });
    }

    //_ Everything from here on is the ImGui window/row rendering (see addon.h).
    SubsWindowDrawTimer drawTimer; //. no-op unless ShowDebug

    ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(TrId("WE_SUBS_WINDOW_TITLE", kSubscriptionsWindowId).c_str(), &ShowSubscriptionsWindow))
    {
        //_ Collapsed (not closed) - still need End() to balance Begin().
        ImGui::End();
        return;
    }

    if (rows.empty())
    {
        bool hasSubscriptions = !(g_SubscribedBasicEvents.empty() && g_SubscribedCyclicSlots.empty());

        if (hasSubscriptions && SubscriptionsHideActive)
        {
            ImGui::TextWrapped("%s", Tr("WE_SUBS_EMPTY_ACTIVE"));
        }
        else if (hasSubscriptions)
        {
            //_ Reachable when everything subscribed is done today, unlike having none at all below.
            ImGui::TextWrapped("%s", Tr("WE_SUBS_EMPTY_DONE"));
        }
        else
        {
            ImGui::TextWrapped("%s", Tr("WE_SUBS_EMPTY_NONE"));
        }
    }
    bool anyRowHovered = false;
    if (!rows.empty())
    {
        for (const auto& row : rows)
            anyRowHovered |= DrawSubscriptionRow(row.name, row.chatCode, row.active, row.secs, row.isWeekly,
                                                  row.isBasic, row.basicId, row.cyclicKey);

        ImGui::Separator();
        ImGui::TextDisabled("%s", Tr("WE_SUBS_HINT_CLICK"));
        ImGui::TextDisabled("%s", Tr("WE_SUBS_HINT_RIGHT_CLICK"));
    }

    //_ Clears a press that released off any row, so a stale key can't match a future row by name.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_leftPressedKey.clear();

    //_ Background "manage subscriptions" entry point - fires on empty content area only; a row's own right-click is already handled inside DrawSubscriptionRow above (anyRowHovered rules this out there).
    if (ImGui::IsWindowHovered() && !anyRowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##we_edit_subs_bg_popup");
    if (ImGui::BeginPopup("##we_edit_subs_bg_popup"))
    {
        if (ImGui::Selectable(Tr("WE_SUBS_EDIT_SUBSCRIPTIONS")))
            OpenEditSubscriptionsWindow();
        ImGui::EndPopup();
    }

    ImGui::End();
}