//################################################################################
// addon_options.cpp
//--------------------------------------------------------------------------------
// AddonOptions()   draws the World Events section of the Nexus options panel
//--------------------------------------------------------------------------------
// Nexus UI callback - draws into a panel Nexus owns, not a standalone window.
// Widgets write directly into g_Events / g_CyclicGroups / g_BasicCategories /
// g_CyclicCategories. There is no explicit "Save" button: everything is written
// to disk on AddonUnload (see addon.cpp), so edits here just live in memory until
// the addon (or the game) closes.
//
// Covers full editing of individual events, cyclic groups/slots, and categories -
// creating, renaming, deleting, recoloring, drag-and-drop categorization, and
// icon assignment. Every scalar setting is edited in the settings window
// (options_window.h).
//
// The widget-drawing helpers themselves (scoped-disable, period widget,
// icon/color pickers, duplicate-name checks, drag-and-drop plumbing, the notify-
// level control, the shared name/context-menu row, search predicates, and the two
// full row drawers) live in addon_options_helpers.h/.cpp - this file is just
// AddonOptions() itself, assembling those pieces into the panel layout.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "addon_options_helpers.h"
#include "events.h"
#include "events_categories.h"
#include "events_storage.h"   //. SlugifyName/UniqueId for new categories
#include "imgui.h"
#include "localization.h"
#include "options_window.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonOptions
//--------------------------------------------------------------------------------
// Draws the button that opens the settings window, then the Event Lists
// CollapsingHeader around the search box and Table 3 (one BeginTable/EndTable
// pair). A CollapsingHeader clips to a single table column, so it wraps the table
// instead of sitting inside it. List mutations (add/remove event, group,
// category) are captured as bools during the row loop and applied afterward, to
// avoid invalidating indices mid-iteration. One search box filters both the Basic
// and Cyclic trees at once.
//--------------------------------------------------------------------------------
void AddonOptions()
{
    OptionsRenderTimer optionsRenderTimer; //. no-op unless ShowDebug

    //_ Entry point to the unified settings window (options_window.h).
    if (ImGui::Button(Tr("WE_OPTWIN_OPEN_BUTTON")))
        OpenOptionsWindow();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
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
        ImGui::TextDisabled("%s", Tr("WE_OPT_RIGHT_CLICK_HINT"));

        //_ Table 3 - Basic Events tree (col 0), Cyclic Events tree (col 1); one search filters both.
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
}