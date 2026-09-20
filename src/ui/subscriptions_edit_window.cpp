//################################################################################
// subscriptions_edit_window.cpp
//--------------------------------------------------------------------------------
// DrawLeanBasicEventRow       one Basic Event row (internal helper)
// DrawLeanCyclicSlotRow       one Cyclic slot row (internal helper)
// DrawLeanCyclicGroupRow      one Cyclic group row, nests slot rows (internal)
// OpenEditSubscriptionsWindow open the window, optionally targeting one row
// RenderEditSubscriptionsWindow   draws the window; no-op unless open
//--------------------------------------------------------------------------------
// See subscriptions_edit_window.h for the window's purpose and scope. Shares its
// search predicates
// (ContainsCaseInsensitive/EventMatchesSearch/GroupMatchesSearch) and the
// category-aware draw-order loop shape with addon_options.cpp's Table 3, but
// swaps DrawNameAndContextMenu (rename/drag-drop/context-menu) for a plain
// CollapsingHeader per category and DrawBasicEventRow/DrawCyclicGroupRow (full
// structural editing) for the lean row drawers below - see
// addon_options_helpers.h for the pieces reused as-is (DrawSubscribeCheckbox,
// DrawNotifyLevelIcon, DrawNotifyLevelButtons, Get/SetBasicEventNotifyLevel and
// friends). Each lean row shows DrawNotifyLevelIcon in front, collapsed - same
// convention as the main panel, for glanceable state - and DrawNotifyLevelButtons
// in the expanded body, for a direct jump to any level; see
// DrawLeanBasicEventRow/DrawLeanCyclicSlotRow below for both.
//--------------------------------------------------------------------------------

#include "subscriptions_edit_window.h"

#include "addon_options_helpers.h" //. DrawSubscribeCheckbox/DrawNotifyLevelIcon/DrawNotifyLevelButtons/search predicates/DisabledBlock
#include "events.h"
#include "events_categories.h"
#include "events_storage.h" //. DisplayName
#include "events_tracking.h"
#include "imgui.h"
#include "localization.h"
#include "options_window.h" //. OpenOptionsWindow, where Live rows are handled
#include "subscriptions.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

//_ Transient window-visibility flag - see the header comment for why this isn't a SETTING().
bool ShowEditSubscriptionsWindow = false;

//********************************************************************************
// EditSubscriptionsTarget
//--------------------------------------------------------------------------------
// kind/basicId/cyclicKey   identity via SubscriptionKind (subscriptions.h);
//                          Basic or Cyclic only, Live goes to the settings window
//--------------------------------------------------------------------------------
// The row a deep-linking open() call wants expanded on the next draw. Consumed
// exactly once by RenderEditSubscriptionsWindow (see s_hasPendingTarget below),
// so a stale target can't keep re-forcing a row open after the user collapses it.
//--------------------------------------------------------------------------------
struct EditSubscriptionsTarget
{
    SubscriptionKind kind = SubscriptionKind::Basic;
    std::string basicId;
    CyclicSubscriptionKey cyclicKey;
};

static EditSubscriptionsTarget s_pendingTarget;
static bool                    s_hasPendingTarget = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// See the header for the two overloads' contracts. The four-argument one hands a
// Live row to the settings window instead of opening this window.
//--------------------------------------------------------------------------------
void OpenEditSubscriptionsWindow()
{
    ShowEditSubscriptionsWindow = true;
    s_hasPendingTarget = false; //. no row to land on - background right-click entry point
}

void OpenEditSubscriptionsWindow(SubscriptionKind kind, const std::string& basicId,
    const CyclicSubscriptionKey& cyclicKey, const std::string& liveEventId)
{
    if (kind == SubscriptionKind::Live)
    {
        OpenOptionsWindow(kind, basicId, cyclicKey, liveEventId);
        return;
    }

    ShowEditSubscriptionsWindow = true;
    s_pendingTarget = EditSubscriptionsTarget{ kind, basicId, cyclicKey };
    s_hasPendingTarget = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLeanBasicEventRow
//--------------------------------------------------------------------------------
// Collapsed: "[notify icon][show-on-map] Event Name", notify icon in front like
// DrawBasicEventRow in the main panel, so level is visible at a glance. Expanded:
// the 4-way DrawNotifyLevelButtons jump grid (direct-jump, no right-click menu
// here) plus a "Done for today" checkbox. forceOpen sets initial expand state via
// ImGuiCond_Always, not _Once: it's only ever true on the single frame a fresh
// deep-link target is consumed (see s_hasPendingTarget below), so _Once's first-
// call-only behavior would silently no-op every later deep-link to the same row.
//--------------------------------------------------------------------------------
static void DrawLeanBasicEventRow(int i, bool forceOpen)
{
    WorldEvent& ev = g_Events[i];

    int notifyLevel = GetBasicEventNotifyLevel(ev.id);
    int newNotifyLevel = DrawNotifyLevelIcon("##edit_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.id, newNotifyLevel);
    ImGui::SameLine();

    DrawSubscribeCheckbox("##edit_show_on_map", ev.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_ON_MAP"));
    ImGui::SameLine();

    if (forceOpen)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    bool open = ImGui::TreeNode("##edit_event_node", "%s", DisplayName(ev));

    if (open)
    {
        //_ Re-reads notifyLevel/newNotifyLevel fresh - the front-of-row icon above may have just changed it this same frame.
        int notifyLevel2 = GetBasicEventNotifyLevel(ev.id);
        int newLevel = DrawNotifyLevelButtons("##edit_notify_buttons", notifyLevel2);
        if (newLevel != notifyLevel2)
            SetBasicEventNotifyLevel(ev.id, newLevel);

        bool doneToday = IsBasicEventMarkedDoneToday(ev.id);
        if (ImGui::Checkbox(Tr("WE_EDIT_DONE_TODAY"), &doneToday))
            ToggleBasicEventDoneToday(ev.id);

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLeanCyclicSlotRow   (pairs with: DrawLeanCyclicGroupRow)
//--------------------------------------------------------------------------------
// Same collapsed/expanded shape as DrawLeanBasicEventRow (notify icon in front,
// visible collapsed; DrawNotifyLevelButtons jump grid in the expanded body),
// keyed by (group id, slot id) instead of a plain name.
//--------------------------------------------------------------------------------
static void DrawLeanCyclicSlotRow(CyclicGroup& grp, int s, bool forceOpen)
{
    CyclicGroup::Slot& slot = grp.slots[s];
    CyclicSubscriptionKey key{ grp.id, slot.id };

    int notifyLevel = GetCyclicSlotNotifyLevel(key);
    int newNotifyLevel = DrawNotifyLevelIcon("##edit_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetCyclicSlotNotifyLevel(key, newNotifyLevel);
    ImGui::SameLine();

    DrawSubscribeCheckbox("##edit_show_slot_on_map", slot.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_OCCURRENCE"));
    ImGui::SameLine();

    if (forceOpen)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always); //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once

    bool open = ImGui::TreeNode("##edit_slot_node", "%s", DisplayName(slot, grp.id));

    if (open)
    {
        //_ Re-reads fresh - the front-of-row icon above may have just changed it this same frame.
        int notifyLevel2 = GetCyclicSlotNotifyLevel(key);
        int newLevel = DrawNotifyLevelButtons("##edit_notify_buttons", notifyLevel2);
        if (newLevel != notifyLevel2)
            SetCyclicSlotNotifyLevel(key, newLevel);

        bool doneToday = IsCyclicSlotMarkedDoneToday(key);
        if (ImGui::Checkbox(Tr("WE_EDIT_DONE_TODAY"), &doneToday))
            ToggleCyclicSlotDoneToday(key);

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLeanCyclicGroupRow
//--------------------------------------------------------------------------------
// Collapsed: the group's own map-visibility checkbox (grp.shown) plus the group
// name - same convention as DrawLeanBasicEventRow/DrawLeanCyclicSlotRow's
// collapsed lines. Expanded: the bulk "subscribe all slots" checkbox
// (allSlotsSubscribed pattern, mirrors DrawCyclicGroupRow in
// addon_options_helpers.cpp) above the nested per-slot list, each drawn via
// DrawLeanCyclicSlotRow. hasForceSlot/forceSlotId identify which one slot (if
// any) should also force itself open once the group itself is opened - used when
// a deep-link target is a specific occurrence, not just "this cycle."
//--------------------------------------------------------------------------------
static void DrawLeanCyclicGroupRow(int i, bool forceOpenGroup, bool hasForceSlot, const std::string& forceSlotId)
{
    CyclicGroup& grp = g_CyclicGroups[i];

    DrawSubscribeCheckbox("##edit_show_group_on_map", grp.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_RING"));
    ImGui::SameLine();

    if (forceOpenGroup)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always); //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once

    bool open = ImGui::TreeNode("##edit_group_node", "%s", DisplayName(grp));

    if (open)
    {
        bool allSlotsSubscribed = !grp.slots.empty() &&
            std::all_of(grp.slots.begin(), grp.slots.end(), [&](const CyclicGroup::Slot& slot)
            {
                return IsCyclicSlotSubscribed(CyclicSubscriptionKey{ grp.id, slot.id });
            });
        if (DrawSubscribeCheckbox("##edit_subscribe_group", allSlotsSubscribed))
        {
            for (const auto& slot : grp.slots)
            {
                CyclicSubscriptionKey key{ grp.id, slot.id };
                //_ Same post-click semantics as DrawCyclicGroupRow: unticking drops every slot to 0, ticking only raises 0 -> 1.
                if (!allSlotsSubscribed)
                    SetCyclicSlotNotifyLevel(key, 0);
                else if (GetCyclicSlotNotifyLevel(key) == 0)
                    SetCyclicSlotNotifyLevel(key, 1);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("WE_TIP_SUBSCRIBE_CYCLE"));
        ImGui::SameLine();
        ImGui::TextUnformatted(Tr("WE_EDIT_SUBSCRIBE_ALL"));

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            ImGui::PushID(s);
            bool forceOpenSlot = hasForceSlot && grp.slots[s].id == forceSlotId;
            DrawLeanCyclicSlotRow(grp, s, forceOpenSlot);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// One tab, "Basic & Cyclic" (first BeginTabBar use in this addon): the original
// search box + 2-column table (Basic Events / Cyclic Events), each a category-
// aware tree exactly like addon_options.cpp's Table 3, minus every structural-
// editing affordance that doesn't belong in a quick-access view - see the file
// header. The pending deep-link target (if any) is consumed once Begin() confirms
// the window drew this frame, forcing its row/category and the tab open for that
// one draw. Esc-to-close is handled by Nexus via GUI_RegisterCloseOnEscape
// (addon.cpp).
//--------------------------------------------------------------------------------
void RenderEditSubscriptionsWindow()
{
    if (!ShowEditSubscriptionsWindow) return;

    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);

    //_ Force-uncollapses so a pending deep-link target is actually visible.
    if (s_hasPendingTarget)
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    std::string windowTitle = TrId("WE_EDIT_SUBS_WINDOW_TITLE", kEditSubscriptionsWindowId);
    bool isOpen = ImGui::Begin(windowTitle.c_str(), &ShowEditSubscriptionsWindow);
    Localization_SyncCloseOnEscape(&ShowEditSubscriptionsWindow, windowTitle);

    if (!isOpen)
    {
        //_ Collapsed, not closed - still balance Begin() with End().
        ImGui::End();
        return;
    }

    std::optional<EditSubscriptionsTarget> pendingTarget;
    if (s_hasPendingTarget)
    {
        pendingTarget = s_pendingTarget;
        s_hasPendingTarget = false;
    }

    if (!ImGui::BeginTabBar("##edit_subs_tabs"))
    {
        ImGui::End();
        return;
    }

    //_ Forces the tab open for the pending target's one consuming frame - see DrawLeanBasicEventRow's header comment for why _SetSelected here, not a _Once-style flag.
    ImGuiTabItemFlags basicCyclicTabFlags = pendingTarget ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

    if (ImGui::BeginTabItem(Tr("WE_EDIT_TAB_BASIC_CYCLIC"), nullptr, basicCyclicTabFlags))
    {
        //_ Transient UI state; filters both trees, same as addon_options.cpp's Table 3 search box.
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText(TrId("WE_OPT_SEARCH_LABEL", "##edit_subs_search").c_str(), searchBuf, sizeof(searchBuf));
        std::string searchQueryLower = searchBuf;
        std::transform(searchQueryLower.begin(), searchQueryLower.end(), searchQueryLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        bool searchActive = !searchQueryLower.empty();

        //_ Edge-triggered: true only the frame search clears, so an auto-opened category re-collapses (deep-link targets excepted via categoryHasTarget).
        static bool s_wasSearchActive = false;
        bool searchJustCleared = s_wasSearchActive && !searchActive;
        s_wasSearchActive = searchActive;

        if (ImGui::BeginTable("##edit_subs_data", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextRow();

            //_ Column 0 - Basic Events, category-aware draw order (categorized members first, then leftovers).
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Tr("WE_OPT_BASIC_EVENTS"));
            ImGui::Separator();

            {
                ImGui::PushID("basic"); //_ Own ID scope so row/category indices can't collide with the Cyclic column below - table columns don't scope IDs on their own.

                std::vector<bool> isCategorized(g_Events.size(), false);

                for (int c = 0; c < (int)g_BasicCategories.size(); c++)
                {
                    Category& cat = g_BasicCategories[c];
                    ImGui::PushID(c);

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

                    bool categoryHasTarget = false;
                    if (pendingTarget && pendingTarget->kind == SubscriptionKind::Basic)
                        for (int mi : memberIndices)
                            if (g_Events[mi].id == pendingTarget->basicId)
                                categoryHasTarget = true;

                    //_ Same search-skip / unconditional-bookkeeping split as addon_options.cpp's Table 3.
                    bool catOpen = false;
                    if (!searchActive || categoryHasMatch)
                    {
                        if (searchActive)
                            ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);
                        else if (categoryHasTarget) //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once
                            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                        else if (searchJustCleared)
                            ImGui::SetNextItemOpen(false, ImGuiCond_Always);

                        catOpen = ImGui::CollapsingHeader(DisplayName(cat, CategoryListKind::Basic));
                    }

                    for (int mi : memberIndices)
                    {
                        isCategorized[mi] = true;

                        bool memberMatches = categoryNameMatches || EventMatchesSearch(g_Events[mi], searchQueryLower);

                        if (catOpen && memberMatches)
                        {
                            ImGui::PushID(mi);
                            bool forceOpen = pendingTarget && pendingTarget->kind == SubscriptionKind::Basic && g_Events[mi].id == pendingTarget->basicId;
                            DrawLeanBasicEventRow(mi, forceOpen);
                            ImGui::PopID();
                        }
                    }

                    ImGui::PopID();
                }

                for (int i = 0; i < (int)g_Events.size(); i++)
                {
                    if (isCategorized[i]) continue;
                    if (!EventMatchesSearch(g_Events[i], searchQueryLower)) continue;

                    ImGui::PushID(i);
                    bool forceOpen = pendingTarget && pendingTarget->kind == SubscriptionKind::Basic && g_Events[i].id == pendingTarget->basicId;
                    DrawLeanBasicEventRow(i, forceOpen);
                    ImGui::PopID();
                }

                ImGui::PopID(); //. closes the "basic" column ID scope
            }

            //_ Column 1 - Cyclic Events, same category-aware shape, nested one level deeper for slots.
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Tr("WE_OPT_CYCLIC_EVENTS"));
            ImGui::Separator();

            {
                ImGui::PushID("cyclic"); //_ Own ID scope, mirrors the Basic column - see its PushID for why this is needed.

                std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

                for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
                {
                    Category& cat = g_CyclicCategories[c];
                    ImGui::PushID(c);

                    bool categoryNameMatches = ContainsCaseInsensitive(DisplayName(cat, CategoryListKind::Cyclic), searchQueryLower);
                    bool categoryHasMatch = categoryNameMatches;
                    if (!categoryHasMatch)
                        for (const std::string& memberId : cat.members)
                            for (const auto& grp : g_CyclicGroups)
                                if (grp.id == memberId && GroupMatchesSearch(grp, searchQueryLower))
                                    categoryHasMatch = true;

                    bool categoryHasTarget = false;
                    if (pendingTarget && pendingTarget->kind == SubscriptionKind::Cyclic)
                        for (const std::string& memberId : cat.members)
                            if (memberId == pendingTarget->cyclicKey.groupId)
                                categoryHasTarget = true;

                    bool catOpen = false;
                    if (!searchActive || categoryHasMatch)
                    {
                        if (searchActive)
                            ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);
                        else if (categoryHasTarget) //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once
                            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                        else if (searchJustCleared)
                            ImGui::SetNextItemOpen(false, ImGuiCond_Always);

                        catOpen = ImGui::CollapsingHeader(DisplayName(cat, CategoryListKind::Cyclic));
                    }

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
                                bool forceOpenGroup = pendingTarget && pendingTarget->kind == SubscriptionKind::Cyclic
                                    && g_CyclicGroups[i].id == pendingTarget->cyclicKey.groupId;
                                DrawLeanCyclicGroupRow(i, forceOpenGroup, forceOpenGroup,
                                    pendingTarget ? pendingTarget->cyclicKey.slotId : std::string());
                                ImGui::PopID();
                            }
                            break;
                        }
                    }

                    ImGui::PopID();
                }

                for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
                {
                    if (isGroupCategorized[i]) continue;
                    if (!GroupMatchesSearch(g_CyclicGroups[i], searchQueryLower)) continue;

                    ImGui::PushID(i);
                    bool forceOpenGroup = pendingTarget && pendingTarget->kind == SubscriptionKind::Cyclic
                        && g_CyclicGroups[i].id == pendingTarget->cyclicKey.groupId;
                    DrawLeanCyclicGroupRow(i, forceOpenGroup, forceOpenGroup,
                        pendingTarget ? pendingTarget->cyclicKey.slotId : std::string());
                    ImGui::PopID();
                }

                ImGui::PopID(); //. closes the "cyclic" column ID scope
            }

            ImGui::EndTable();
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}