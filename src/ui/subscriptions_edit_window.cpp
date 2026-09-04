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
#include "events_live.h" //. g_LiveEvents, for the Live Events tab
#include "events_tracking.h"
#include "imgui.h"
#include "settings.h" //. Gw2ApiKey, gates the live-event subscribe checkbox below
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
// kind/basicName/cyclicKey/liveEventId   identity, four-way via
//                                          SubscriptionKind (subscriptions.h)
//--------------------------------------------------------------------------------
// The row a deep-linking open() call wants expanded on the next draw. Consumed
// exactly once by RenderEditSubscriptionsWindow (see s_hasPendingTarget below),
// so a stale target can't keep re-forcing a row open after the user collapses it.
//--------------------------------------------------------------------------------
struct EditSubscriptionsTarget
{
    SubscriptionKind kind = SubscriptionKind::Basic;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
    std::string liveEventId;
};

static EditSubscriptionsTarget s_pendingTarget;
static bool                    s_hasPendingTarget = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// See the header for the two overloads' contracts.
//--------------------------------------------------------------------------------
void OpenEditSubscriptionsWindow()
{
    ShowEditSubscriptionsWindow = true;
    s_hasPendingTarget = false; //. no row to land on - background right-click entry point
}

void OpenEditSubscriptionsWindow(SubscriptionKind kind, const std::string& basicName,
    const CyclicSubscriptionKey& cyclicKey, const std::string& liveEventId)
{
    ShowEditSubscriptionsWindow = true;
    s_pendingTarget = EditSubscriptionsTarget{ kind, basicName, cyclicKey, liveEventId };
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

    int notifyLevel = GetBasicEventNotifyLevel(ev.name);
    int newNotifyLevel = DrawNotifyLevelIcon("##edit_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.name, newNotifyLevel);
    ImGui::SameLine();

    DrawSubscribeCheckbox("##edit_show_on_map", ev.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show on the map overlay\n(Subscriptions bar/window are unaffected)");
    ImGui::SameLine();

    if (forceOpen)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    bool open = ImGui::TreeNode("##edit_event_node", "%s", ev.name.empty() ? "(unnamed)" : ev.name.c_str());

    if (open)
    {
        //_ Re-reads notifyLevel/newNotifyLevel fresh - the front-of-row icon above may have just changed it this same frame.
        int notifyLevel2 = GetBasicEventNotifyLevel(ev.name);
        int newLevel = DrawNotifyLevelButtons("##edit_notify_buttons", notifyLevel2);
        if (newLevel != notifyLevel2)
            SetBasicEventNotifyLevel(ev.name, newLevel);

        bool doneToday = IsBasicEventMarkedDoneToday(ev.name);
        if (ImGui::Checkbox("Done for today", &doneToday))
            ToggleBasicEventDoneToday(ev.name);

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLeanCyclicSlotRow   (pairs with: DrawLeanCyclicGroupRow)
//--------------------------------------------------------------------------------
// Same collapsed/expanded shape as DrawLeanBasicEventRow (notify icon in front,
// visible collapsed; DrawNotifyLevelButtons jump grid in the expanded body),
// keyed by (group name, slot offset) instead of a plain name.
//--------------------------------------------------------------------------------
static void DrawLeanCyclicSlotRow(CyclicGroup& grp, int s, bool forceOpen)
{
    CyclicGroup::Slot& slot = grp.slots[s];
    CyclicSubscriptionKey key{ grp.name, slot.offset };

    int notifyLevel = GetCyclicSlotNotifyLevel(key);
    int newNotifyLevel = DrawNotifyLevelIcon("##edit_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetCyclicSlotNotifyLevel(key, newNotifyLevel);
    ImGui::SameLine();

    DrawSubscribeCheckbox("##edit_show_slot_on_map", slot.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show/hide this occurrence on the map overlay");
    ImGui::SameLine();

    if (forceOpen)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always); //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once

    bool open = ImGui::TreeNode("##edit_slot_node", "%s", slot.name.empty() ? "(unnamed)" : slot.name.c_str());

    if (open)
    {
        //_ Re-reads fresh - the front-of-row icon above may have just changed it this same frame.
        int notifyLevel2 = GetCyclicSlotNotifyLevel(key);
        int newLevel = DrawNotifyLevelButtons("##edit_notify_buttons", notifyLevel2);
        if (newLevel != notifyLevel2)
            SetCyclicSlotNotifyLevel(key, newLevel);

        bool doneToday = IsCyclicSlotMarkedDoneToday(key);
        if (ImGui::Checkbox("Done for today", &doneToday))
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
// DrawLeanCyclicSlotRow. hasForceSlot/forceSlotOffset identify which one slot (if
// any) should also force itself open once the group itself is opened - used when
// a deep-link target is a specific occurrence, not just "this cycle."
//--------------------------------------------------------------------------------
static void DrawLeanCyclicGroupRow(int i, bool forceOpenGroup, bool hasForceSlot, int forceSlotOffset)
{
    CyclicGroup& grp = g_CyclicGroups[i];

    DrawSubscribeCheckbox("##edit_show_group_on_map", grp.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show/hide this entire ring on the map overlay\n(no circle drawn at all while unchecked)");
    ImGui::SameLine();

    if (forceOpenGroup)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always); //. see DrawLeanBasicEventRow's header comment for why _Always, not _Once

    bool open = ImGui::TreeNode("##edit_group_node", "%s", grp.name.empty() ? "(unnamed)" : grp.name.c_str());

    if (open)
    {
        bool allSlotsSubscribed = !grp.slots.empty() &&
            std::all_of(grp.slots.begin(), grp.slots.end(), [&](const CyclicGroup::Slot& slot)
            {
                return IsCyclicSlotSubscribed(CyclicSubscriptionKey{ grp.name, slot.offset });
            });
        if (DrawSubscribeCheckbox("##edit_subscribe_group", allSlotsSubscribed))
        {
            for (const auto& slot : grp.slots)
            {
                CyclicSubscriptionKey key{ grp.name, slot.offset };
                //_ Same post-click semantics as DrawCyclicGroupRow: unticking drops every slot to 0, ticking only raises 0 -> 1.
                if (!allSlotsSubscribed)
                    SetCyclicSlotNotifyLevel(key, 0);
                else if (GetCyclicSlotNotifyLevel(key) == 0)
                    SetCyclicSlotNotifyLevel(key, 1);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Subscribe/unsubscribe every occurrence in this cycle at once\n(checked only when all of them already are)");
        ImGui::SameLine();
        ImGui::TextUnformatted("Subscribe all");

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            ImGui::PushID(s);
            bool forceOpenSlot = hasForceSlot && grp.slots[s].offset == forceSlotOffset;
            DrawLeanCyclicSlotRow(grp, s, forceOpenSlot);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLeanLiveEventRow
//--------------------------------------------------------------------------------
// One flat row per compiled-in LiveEvent. No notify-level ladder - subscribing IS
// the toast opt-in, one flat list (subscriptions.h) - and no category tree
// (g_LiveEvents carries none), so unlike DrawLeanBasicEventRow/
// DrawLeanCyclicSlotRow there's nothing to collapse: subscribe checkbox, name,
// and the done-today toggle all sit on one line, always visible. The subscribe
// checkbox itself is disabled while Gw2ApiKey (settings.h) is empty - region-
// wide toast delivery needs GetLiveEventsRegion (gw2_api.h), which needs that key
// now that Mumble no longer provides one.
//--------------------------------------------------------------------------------
static void DrawLeanLiveEventRow(const LiveEvent& ev)
{
    bool subscribed = IsLiveEventSubscribed(ev.eventId);
    DisabledBlock(Gw2ApiKey.empty())
    {
        if (DrawSubscribeCheckbox("##edit_live_subscribe", subscribed))
            ToggleLiveEventSubscription(ev.eventId);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(Gw2ApiKey.empty()
            ? "Requires a GW2 API key (options panel) - region-wide toast\ndelivery needs it to tell NA and EU apart."
            : "Subscribe to region-wide toast notifications for this event,\nregardless of which map you're currently on.");
    }
    ImGui::SameLine();

    ImGui::TextUnformatted(ev.name.empty() ? "(unnamed)" : ev.name.c_str());
    ImGui::SameLine();

    bool doneToday = IsLiveEventMarkedDoneToday(ev.eventId);
    std::string doneLabel = "Done for today##edit_live_done";
    if (ImGui::Checkbox(doneLabel.c_str(), &doneToday))
        ToggleLiveEventDoneToday(ev.eventId);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderEditSubscriptionsWindow
//--------------------------------------------------------------------------------
// Two tabs (first BeginTabBar use in this addon): "Basic & Cyclic", the original
// search box + 2-column table (Basic Events / Cyclic Events), each a category-
// aware tree exactly like addon_options.cpp's Table 3, minus every structural-
// editing affordance that doesn't belong in a quick-access view - see the file
// header; and "Live Events", a flat DrawLeanLiveEventRow per g_LiveEvents entry,
// no search box - the compiled-in roster is short enough not to need one. The
// pending deep-link target (if any) is consumed once Begin() confirms the window
// drew this frame, forcing its row/category and matching tab open for that one
// draw. Esc-to-close is handled by Nexus via GUI_RegisterCloseOnEscape
// (addon.cpp), not an in-window key check.
//--------------------------------------------------------------------------------
void RenderEditSubscriptionsWindow()
{
    if (!ShowEditSubscriptionsWindow) return;

    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);

    //_ Force-uncollapses so a pending deep-link target is actually visible.
    if (s_hasPendingTarget)
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    if (!ImGui::Begin(kEditSubscriptionsWindowTitle, &ShowEditSubscriptionsWindow))
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

    //_ Forces whichever tab matches the pending target open for its one consuming frame - see DrawLeanBasicEventRow's header comment for why _SetSelected here, not a _Once-style flag.
    ImGuiTabItemFlags basicCyclicTabFlags = ImGuiTabItemFlags_None;
    ImGuiTabItemFlags liveTabFlags        = ImGuiTabItemFlags_None;
    if (pendingTarget)
    {
        if (pendingTarget->kind == SubscriptionKind::Live)
            liveTabFlags = ImGuiTabItemFlags_SetSelected;
        else
            basicCyclicTabFlags = ImGuiTabItemFlags_SetSelected;
    }

    if (ImGui::BeginTabItem("Basic & Cyclic", nullptr, basicCyclicTabFlags))
    {
        //_ Transient UI state; filters both trees, same as addon_options.cpp's Table 3 search box.
        static char searchBuf[128] = "";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Search##edit_subs_search", searchBuf, sizeof(searchBuf));
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
            ImGui::TextUnformatted("Basic Events");
            ImGui::Separator();

            {
                ImGui::PushID("basic"); //_ Own ID scope so row/category indices can't collide with the Cyclic column below - table columns don't scope IDs on their own.

                std::vector<bool> isCategorized(g_Events.size(), false);

                for (int c = 0; c < (int)g_BasicCategories.size(); c++)
                {
                    Category& cat = g_BasicCategories[c];
                    ImGui::PushID(c);

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

                    bool categoryHasTarget = false;
                    if (pendingTarget && pendingTarget->kind == SubscriptionKind::Basic)
                        for (int mi : memberIndices)
                            if (g_Events[mi].name == pendingTarget->basicName)
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

                        catOpen = ImGui::CollapsingHeader(cat.name.empty() ? "(unnamed)" : cat.name.c_str());
                    }

                    for (int mi : memberIndices)
                    {
                        isCategorized[mi] = true;

                        bool memberMatches = categoryNameMatches || EventMatchesSearch(g_Events[mi], searchQueryLower);

                        if (catOpen && memberMatches)
                        {
                            ImGui::PushID(mi);
                            bool forceOpen = pendingTarget && pendingTarget->kind == SubscriptionKind::Basic && g_Events[mi].name == pendingTarget->basicName;
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
                    bool forceOpen = pendingTarget && pendingTarget->kind == SubscriptionKind::Basic && g_Events[i].name == pendingTarget->basicName;
                    DrawLeanBasicEventRow(i, forceOpen);
                    ImGui::PopID();
                }

                ImGui::PopID(); //. closes the "basic" column ID scope
            }

            //_ Column 1 - Cyclic Events, same category-aware shape, nested one level deeper for slots.
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted("Cyclic Events");
            ImGui::Separator();

            {
                ImGui::PushID("cyclic"); //_ Own ID scope, mirrors the Basic column - see its PushID for why this is needed.

                std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

                for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
                {
                    Category& cat = g_CyclicCategories[c];
                    ImGui::PushID(c);

                    bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
                    bool categoryHasMatch = categoryNameMatches;
                    if (!categoryHasMatch)
                        for (const std::string& memberName : cat.members)
                            for (const auto& grp : g_CyclicGroups)
                                if (grp.name == memberName && GroupMatchesSearch(grp, searchQueryLower))
                                    categoryHasMatch = true;

                    bool categoryHasTarget = false;
                    if (pendingTarget && pendingTarget->kind == SubscriptionKind::Cyclic)
                        for (const std::string& memberName : cat.members)
                            if (memberName == pendingTarget->cyclicKey.groupName)
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

                        catOpen = ImGui::CollapsingHeader(cat.name.empty() ? "(unnamed)" : cat.name.c_str());
                    }

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
                                bool forceOpenGroup = pendingTarget && pendingTarget->kind == SubscriptionKind::Cyclic
                                    && g_CyclicGroups[i].name == pendingTarget->cyclicKey.groupName;
                                DrawLeanCyclicGroupRow(i, forceOpenGroup, forceOpenGroup,
                                    pendingTarget ? pendingTarget->cyclicKey.slotOffset : 0);
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
                        && g_CyclicGroups[i].name == pendingTarget->cyclicKey.groupName;
                    DrawLeanCyclicGroupRow(i, forceOpenGroup, forceOpenGroup,
                        pendingTarget ? pendingTarget->cyclicKey.slotOffset : 0);
                    ImGui::PopID();
                }

                ImGui::PopID(); //. closes the "cyclic" column ID scope
            }

            ImGui::EndTable();
        }

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Live Events", nullptr, liveTabFlags))
    {
        if (g_LiveEvents.empty())
        {
            ImGui::TextDisabled("No live events compiled in.");
        }
        else
        {
            for (const LiveEvent& ev : g_LiveEvents)
            {
                ImGui::PushID(ev.eventId.c_str());
                DrawLeanLiveEventRow(ev);
                ImGui::PopID();
            }
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}