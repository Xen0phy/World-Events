//################################################################################
// options_events.cpp   (see: options_events.h)
//--------------------------------------------------------------------------------
// kMinSearchWidthEm    narrowest the search box may shrink to
// kLinkLifeFrames      frames a deep link waits for its sub-tab to show
// s_deepMode           Quick/Deep toggle state
// s_searchBuf          text of the search box
// s_basicWasSearching  Basic list had a query on its last drawn frame
// s_cyclicWasSearching Cyclic list had a query on its last drawn frame
// s_link               row the last deep link named
// s_linkPending        that link has not reached its row yet
// s_linkFrame          frame count when the link arrived
// s_basicCategoryEdit  Basic list's category edit state
// s_cyclicCategoryEdit Cyclic list's category edit state
// SegmentWidth         pixel width of one Quick/Deep button
// DrawModeSegment      one Quick/Deep button
// DrawRightClickHint   the hint text, right-aligned when it fits
// DrawTopStrip         search, toggle, Reset, Restore and the hint
// DrawIconFileCombo    texture-file combo used by the Cyclic texture rows
// DrawBulkIconPicker   Set all icons combo of the Basic settings header
// DrawSharedSettings   body of the Shared settings header
// DrawBasicSettings    body of the Basic event settings header
// DrawCyclicSettings   body of the Cyclic event settings header
// ContainsCaseInsensitive/EventMatchesSearch/GroupMatchesSearch
//                      search predicates for the two lists
// SearchQueryLower     search box text, lowercased for the predicates
// LinkedItemId/SelectIfLinked/ConsumeLink
//                      list-level side of a pending deep link
// BasicRowMode/GroupRowMode
//                      RowMode for one Basic event / Cyclic group
// DrawListToolbar      line above a list: name, add item, add category
// AddCategory          append an unnamed category and open its name box
// AddBasicEvent/AddCyclicGroup
//                      append an unnamed event or group with default values
// DrawCategorizedList  the one category-aware list loop, takes a row renderer
// DrawBasicList        toolbar and event list under the Basic settings header
// DrawCyclicList       toolbar and group list under the Cyclic settings header
//--------------------------------------------------------------------------------

#include "options_events.h"

#include "events.h" //. g_Events, g_CyclicGroups
#include "events_categories.h" //. Category, DisplayName
#include "events_storage.h" //. DisplayName for events, groups and slots, NewUniqueId
#include "imgui.h"
#include "imgui_internal.h" //. GetActiveID, ClearActiveID, for the deep-link focus reset
#include "localization.h"
#include "maprender.h" //. GetEventIconFilenames
#include "options_events_rows.h" //. row drawers, DrawNameAndContextMenu, drag-drop targets
#include "options_widgets.h" //. Tooltip, DisabledBlock, SubToggleIndent, DrawFileCombo, kAlphaSwatchFlags
#include "reset_defaults.h" //. DrawResetToDefaultsButton/Popup, DrawRestoreMissingButton
#include "settings.h"
#include "texture_whitener.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

//_ Floor for the search box width in multiples of the font size, so a narrow window never squeezes it to nothing.
static constexpr float kMinSearchWidthEm = 6.0f;

//_ Covers the frame a sub-tab selection takes to show, plus one spare.
static constexpr int kLinkLifeFrames = 3;

//_ Not persisted: the window opens in Quick every time (ResetOptionsEventsView).
static bool s_deepMode = false;

//_ Not persisted; emptied by ResetOptionsEventsView.
static char s_searchBuf[128] = "";

//_ Per list, so a tab switch during a search still collapses that tab's categories once the query clears.
static bool s_basicWasSearching = false;

//_ Same as s_basicWasSearching, for the Cyclic list.
static bool s_cyclicWasSearching = false;

//_ Kept after the link lands, since the flash still needs its row.
static OptionsDeepLink s_link;

//_ Cleared by ConsumeLink, or by expiry when the window closes first.
static bool s_linkPending = false;

//_ Lets a link that never reached its row expire instead of firing later.
static int s_linkFrame = 0;

//_ Category name editing, keyed by category index; not persisted, one per list so a rename in progress on one tab survives a switch to the other.
static NameEditState s_basicCategoryEdit;
static NameEditState s_cyclicCategoryEdit;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SegmentWidth
//--------------------------------------------------------------------------------
// Label width plus horizontal frame padding. The search box is sized from this
// before the toggle is drawn, since the box comes first on the row and fills
// whatever the toggle leaves.
//--------------------------------------------------------------------------------
static float SegmentWidth(const char* label)
{
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawModeSegment
//--------------------------------------------------------------------------------
// Manual hit-test and ImDrawList (ImGui 1.80 has no segmented control), the same
// approach as DrawRailButton (options_window.cpp). The selected segment gets the
// filled and outlined Header frame that DrawNotifyLevelButtons uses for its
// active box. tooltipId doubles as the ImGui ID, so the ID is the same in every
// language. Returns true on the frame the segment is clicked.
//--------------------------------------------------------------------------------
static bool DrawModeSegment(const char* label, const char* tooltipId, bool selected)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 size(SegmentWidth(label), ImGui::GetFrameHeight());

    ImGui::PushID(tooltipId);
    const bool clicked = ImGui::InvisibleButton("##mode_segment", size);
    ImGui::PopID();

    const bool hovered = ImGui::IsItemHovered();
    Tooltip(Tr(tooltipId));

    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (selected)
    {
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
        dl->AddRect(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderActive), style.FrameRounding, ImDrawCornerFlags_All, 1.5f);
    }
    else
    {
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg), style.FrameRounding);
    }

    dl->AddText(ImVec2(rmin.x + style.FramePadding.x, rmin.y + style.FramePadding.y),
        ImGui::GetColorU32(ImGuiCol_Text), label);

    return clicked;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawRightClickHint
//--------------------------------------------------------------------------------
// Call directly after the last item of the button row. When the hint fits in what
// is left of that row it is pushed against the right edge; otherwise it wraps on
// a line of its own.
//--------------------------------------------------------------------------------
static void DrawRightClickHint()
{
    const char* hint = Tr("WE_OPT_RIGHT_CLICK_HINT");
    const float hintWidth = ImGui::CalcTextSize(hint).x;

    //_ The cursor is at the start of the next line here, so start plus available width is the content's right edge.
    const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float slack = rightEdge - ImGui::GetItemRectMax().x - ImGui::GetStyle().ItemSpacing.x - hintWidth;

    if (slack >= 0.0f)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack);
        ImGui::TextDisabled("%s", hint);
    }
    else
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", hint);
        ImGui::PopTextWrapPos();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawTopStrip
//--------------------------------------------------------------------------------
// Shown above both sub-tabs. Row 1: the search box, stretched to fill the row,
// and the Quick/Deep toggle at its right end. Row 2: the Reset and Restore
// buttons (reset_defaults.h) and the right-click hint. The Reset confirm popup is
// drawn here every frame and must share the ID scope of its button, so neither
// call sits inside a PushID.
//--------------------------------------------------------------------------------
static void DrawTopStrip()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* quickLabel = Tr("WE_OPTWIN_EVENTS_QUICK");
    const char* deepLabel  = Tr("WE_OPTWIN_EVENTS_DEEP");

    const float toggleWidth = SegmentWidth(quickLabel) + style.ItemInnerSpacing.x + SegmentWidth(deepLabel);
    const float searchWidth = std::max(ImGui::GetContentRegionAvail().x - toggleWidth - style.ItemSpacing.x,
        ImGui::GetFontSize() * kMinSearchWidthEm);

    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##events_search", Tr("WE_OPT_SEARCH_LABEL"), s_searchBuf, sizeof(s_searchBuf));

    ImGui::SameLine();
    if (DrawModeSegment(quickLabel, "WE_OPTWIN_EVENTS_QUICK_TIP", !s_deepMode))
        s_deepMode = false;
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (DrawModeSegment(deepLabel, "WE_OPTWIN_EVENTS_DEEP_TIP", s_deepMode))
        s_deepMode = true;

    DrawResetToDefaultsButton();
    DrawResetToDefaultsPopup(); //. no-op unless popup open

    ImGui::SameLine();
    DrawRestoreMissingButton();

    DrawRightClickHint();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawIconFileCombo
//--------------------------------------------------------------------------------
// DrawFileCombo over GetEventIconFilenames() (maprender.h) with a None entry.
// labelKey may be nullptr for a combo without a visible label.
//--------------------------------------------------------------------------------
static void DrawIconFileCombo(const char* labelKey, const char* id, std::string& filename)
{
    const std::string label = labelKey ? TrId(labelKey, id) : std::string(id);
    DrawFileCombo(label.c_str(), "WE_OPT_NONE", GetEventIconFilenames(), filename);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBulkIconPicker
//--------------------------------------------------------------------------------
// One dropdown that sets ev.iconTexture for every event index in `targetIndices`
// at once. Display state before the user touches it: if every target already
// shares the exact same iconTexture (including "all empty", i.e. all using the
// plain dot), that shared value is shown selected. If they disagree, a "(mixed)"
// entry is shown instead, purely as a status display: selecting any OTHER entry
// applies that choice to every target, and "(mixed)" naturally drops out of the
// list once the state resolves to non-mixed.
//--------------------------------------------------------------------------------
static void DrawBulkIconPicker(const char* label, const std::vector<int>& targetIndices)
{
    if (targetIndices.empty()) return;

    bool mixed = false;
    std::string shared = g_Events[targetIndices[0]].iconTexture;
    for (int idx : targetIndices)
        if (g_Events[idx].iconTexture != shared) { mixed = true; break; }

    const std::vector<std::string>& iconFiles = GetEventIconFilenames();
    std::vector<const char*> iconLabels;
    if (mixed) iconLabels.push_back(Tr("WE_ICON_MIXED"));
    iconLabels.push_back(Tr("WE_ICON_DOT"));
    for (const auto& fn : iconFiles)
        iconLabels.push_back(fn.c_str());

    //_ "Dot"'s index is 0, or 1 if "(mixed)" occupies slot 0; filenames are offset by whichever lead entries precede them.
    int dotIndex = mixed ? 1 : 0;
    int iconIndex = mixed ? 0 : dotIndex;
    if (!mixed && !shared.empty())
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == shared)
                iconIndex = dotIndex + 1 + k;

    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo(label, &iconIndex, iconLabels.data(), (int)iconLabels.size()))
    {
        //_ Combo only returns true when the result differs from the input, so iconIndex can't still be "(mixed)" here.
        std::string newIcon = (iconIndex == dotIndex) ? std::string() : iconFiles[iconIndex - dotIndex - 1];
        for (int idx : targetIndices)
            g_Events[idx].iconTexture = newIcon;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSharedSettings
//--------------------------------------------------------------------------------
// Settings that apply to Basic and Cyclic alike. One zoom curve scales Basic
// markers and Cyclic rings (GetEventZoomSizeMultiplier, maprender.h), so the
// labels name both; the settings behind it are named BasicEventZoom* but drive
// both kinds. The Texture Whitener works on the one textures folder both kinds
// pick from; its popup is drawn beside its button so both share one ID scope.
//--------------------------------------------------------------------------------
static void DrawSharedSettings()
{
    ImGui::Checkbox(TrId("WE_OPT_GROW_MARKERS_ZOOM", "##basic_zoom_scaling_enabled").c_str(), &BasicEventZoomScalingEnabled);

    DisabledBlock(!BasicEventZoomScalingEnabled)
    {
        SubToggleIndent indent;

        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat(TrId("WE_OPT_START_GROWING_AT", "##basic_zoom_start_pct").c_str(), &BasicEventZoomStartPct, 1.0f, 0.0f, 100.0f, "%.0f%%");

        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat(TrId("WE_OPT_MAX_SIZE_AT_ZOOM", "##basic_zoom_max_mult").c_str(), &BasicEventZoomMaxMultiplier, 1.0f, 1.0f, 4.0f, "%.1fx");
    }

    ImGui::Spacing();
    DrawTextureWhitenerButton();
    DrawTextureWhitenerPopup(); //. no-op unless popup open

    ImGui::Separator();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicSettings
//--------------------------------------------------------------------------------
// Everything here is global to all Basic events; per-event values live in the
// rows.
//--------------------------------------------------------------------------------
static void DrawBasicSettings()
{
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
                BasicEventTimeFilterMinutes = stepIndex * 15;
        }
    }

    //_ One shared color set for every Basic Event, matching the active/soon/waiting dot and icon-tint states.
    ImGui::ColorEdit4(TrId("WE_OPT_ACTIVE",  "##basic_color_active").c_str(),  BasicEventColorActive,  kAlphaSwatchFlags);
    ImGui::ColorEdit4(TrId("WE_OPT_SOON",    "##basic_color_soon").c_str(),    BasicEventColorSoon,    kAlphaSwatchFlags);
    ImGui::ColorEdit4(TrId("WE_OPT_WAITING", "##basic_color_waiting").c_str(), BasicEventColorWaiting, kAlphaSwatchFlags);

    //_ Independent settings, not derived from one another - dot and icon sizes can differ freely.
    ImGui::SetNextItemWidth(50.0f);
    ImGui::DragFloat(TrId("WE_OPT_DOT_RADIUS", "##basic_dot_radius").c_str(), &BasicEventDotRadius, 1.0f, 2.0f, 30.0f, "%.0f px");

    ImGui::SetNextItemWidth(50.0f);
    ImGui::DragFloat(TrId("WE_OPT_ICON_SIZE", "##basic_icon_size").c_str(), &BasicEventIconSize, 1.0f, 2.0f, 40.0f, "%.0f px");

    //_ Applies to every Basic Event regardless of category; there is no per-category picker.
    std::vector<int> allIndices(g_Events.size());
    for (int i = 0; i < (int)g_Events.size(); i++)
        allIndices[i] = i;
    DrawBulkIconPicker(TrId("WE_OPT_SET_ALL_ICONS", "###bulk_icon_all").c_str(), allIndices);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicSettings
//--------------------------------------------------------------------------------
// Ring, window and texture settings shared by every Cyclic group. Radius and
// Thickness pull each other in so the band never exceeds the ring's diameter; the
// future and past windows share one 360-degree budget.
//--------------------------------------------------------------------------------
static void DrawCyclicSettings()
{
    ImGui::Checkbox(Tr("WE_OPT_SHOW_CYCLIC_ON_MAP"), &ShowCyclicOverlay);

    DisabledBlock(!ShowCyclicOverlay)
    {
        SubToggleIndent indent;

        ImGui::TextDisabled("%s", Tr("WE_OPT_RING_APPEARANCE"));
        ImGui::SetNextItemWidth(50.0f);
        ImGui::DragFloat(Tr("WE_OPT_RADIUS"), &CyclicRadius, 1.0f, 5.0f, 50.0f, "%.0f px");
        if (CyclicRadius < CyclicThickness / 2) { CyclicThickness = CyclicRadius * 2; }
        ImGui::SetNextItemWidth(50.0f);
        ImGui::DragFloat(Tr("WE_OPT_THICKNESS"), &CyclicThickness, 1.0f, 5.0f, 100.0f, "%.0f px");
        if (CyclicThickness > CyclicRadius * 2) { CyclicRadius = CyclicThickness / 2; }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_ENTRY_EXIT_WINDOW"));
        ImGui::SetNextItemWidth(50.0f);
        ImGui::DragFloat(Tr("WE_OPT_FUTURE_WINDOW"), &CyclicMaxFutureDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
        if (CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f) { CyclicMaxPastDeg = 360 - CyclicMaxFutureDeg; }
        Tooltip(Tr("WE_TIP_FUTURE_WINDOW"));
        ImGui::SetNextItemWidth(50.0f);
        ImGui::DragFloat(Tr("WE_OPT_PAST_WINDOW"), &CyclicMaxPastDeg, 1.0f, 0.0f, 360.0f, "%.0f deg");
        if (CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f) { CyclicMaxFutureDeg = 360 - CyclicMaxPastDeg; }
        Tooltip(Tr("WE_TIP_PAST_WINDOW"));

        ImGui::Checkbox(TrId("WE_OPT_FADE_PAST_EVENTS", "##cyclic_past_fade_enabled").c_str(), &CyclicPastFadeEnabled);
        Tooltip(Tr("WE_TIP_FADE_PAST_EVENTS"));

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_HAND"));
        ImGui::ColorEdit4(TrId("WE_OPT_COLOR", "##cyclic_hand_color").c_str(), CyclicHandColor, kAlphaSwatchFlags);
        Tooltip(Tr("WE_TIP_HAND_COLOR"));

        ImGui::Checkbox(TrId("WE_OPT_USE_TEXTURE", "##cyclic_hand_image_enabled").c_str(), &CyclicHandImageEnabled);
        Tooltip(Tr("WE_TIP_HAND_USE_TEXTURE"));

        DisabledBlock(!CyclicHandImageEnabled)
        {
            SubToggleIndent handIndent;
            DrawIconFileCombo(nullptr, "##cyclic_hand_image_file", CyclicHandImageFilename);

            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_WIDTH", "##cyclic_hand_image_width").c_str(), &CyclicHandImageWidth, 1.0f, 2.0f, 60.0f, "%.0f px");
            Tooltip(Tr("WE_TIP_HAND_TEXTURE_WIDTH"));
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_RING_EDGE_TEXTURE"));
        ImGui::Checkbox("##cyclic_ring_image_enabled", &CyclicRingImageEnabled);
        Tooltip(Tr("WE_TIP_RING_EDGE_TEXTURE"));

        DisabledBlock(!CyclicRingImageEnabled)
        {
            ImGui::SameLine();
            DrawIconFileCombo("WE_OPT_TEXTURE", "##cyclic_ring_image_file", CyclicRingImageFilename);

            SubToggleIndent ringIndent;

            //_ The only control over the band's on-screen thickness.
            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_THICKNESS", "##cyclic_ring_image_thickness").c_str(), &CyclicRingImageThickness, 0.5f, 1.0f, 80.0f, "%.1f px");
            Tooltip(Tr("WE_TIP_RING_TEXTURE_THICKNESS"));

            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_OFFSET", "##cyclic_ring_image_offset").c_str(), &CyclicRingImageOffset, 0.1f, -5.0f, 5.0f, "%.1f px");
            Tooltip(Tr("WE_TIP_RING_TEXTURE_OFFSET"));
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_FILL_TEXTURE"));
        ImGui::Checkbox("##cyclic_fill_image_enabled", &CyclicFillImageEnabled);
        Tooltip(Tr("WE_TIP_FILL_TEXTURE"));

        DisabledBlock(!CyclicFillImageEnabled)
        {
            ImGui::SameLine();
            DrawIconFileCombo(nullptr, "##cyclic_fill_image_file", CyclicFillImageFilename);

            SubToggleIndent fillIndent;
            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_OPACITY", "##cyclic_fill_image_opacity").c_str(), &CyclicFillImageOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContainsCaseInsensitive / EventMatchesSearch / GroupMatchesSearch
//--------------------------------------------------------------------------------
// One shared query, from the single search box, filters both Basic Events and
// Cyclic Events at once. Matching is a case-insensitive substring test, and for
// Cyclic Events checks BOTH the group's own name AND every one of its slot names,
// so typing "Crash Site" finds Dry Top even though "Dry Top" itself doesn't
// contain that text.
//--------------------------------------------------------------------------------
static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
{
    if (needleLower.empty()) return true; //. empty query matches everything
    std::string haystackLower = haystack;
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return haystackLower.find(needleLower) != std::string::npos;
}

static bool EventMatchesSearch(const WorldEvent& ev, const std::string& queryLower)
{
    return ContainsCaseInsensitive(DisplayName(ev), queryLower);
}

static bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower)
{
    if (ContainsCaseInsensitive(DisplayName(grp), queryLower)) return true;
    for (const auto& slot : grp.slots)
        if (ContainsCaseInsensitive(DisplayName(slot, grp.id), queryLower)) return true;
    return false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SearchQueryLower
//--------------------------------------------------------------------------------
// The form EventMatchesSearch and GroupMatchesSearch expect. Call after
// DrawTopStrip so the box's text from this frame is used.
//--------------------------------------------------------------------------------
static std::string SearchQueryLower()
{
    std::string query = s_searchBuf;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return query;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LinkedItemId/SelectIfLinked/ConsumeLink
//--------------------------------------------------------------------------------
// The list-level side of a pending deep link; kind is the list asking, Basic or
// Cyclic. LinkedItemId returns the id of the event or group the link names, or an
// empty string when the link is for the other list or has been applied; the list
// loop opens that item's category. SelectIfLinked returns the BeginTabItem flag
// that selects the list's sub-tab. Selecting takes a frame to show, so the link
// stays pending until ConsumeLink, called after the list has been drawn.
//--------------------------------------------------------------------------------
static const std::string& LinkedItemId(SubscriptionKind kind)
{
    static const std::string none;
    if (!s_linkPending || s_link.kind != kind)
        return none;
    return kind == SubscriptionKind::Basic ? s_link.basicId : s_link.cyclicKey.groupId;
}

static ImGuiTabItemFlags SelectIfLinked(SubscriptionKind kind)
{
    return (s_linkPending && s_link.kind == kind) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
}

static void ConsumeLink(SubscriptionKind kind)
{
    if (s_link.kind == kind)
        s_linkPending = false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicRowMode/GroupRowMode
//--------------------------------------------------------------------------------
// The RowMode (options_events_rows.h) for one row: the Quick/Deep toggle, and
// whether the pending deep link names the row. A group is linked by a link to it
// or to any of its slots; the slot id rides along for the row to tell them apart.
//--------------------------------------------------------------------------------
static RowMode BasicRowMode(const WorldEvent& ev)
{
    const bool linked = s_link.kind == SubscriptionKind::Basic && s_link.basicId == ev.id;
    return RowMode{ s_deepMode, linked, s_linkPending, nullptr };
}

static RowMode GroupRowMode(const CyclicGroup& grp)
{
    const bool linked = s_link.kind == SubscriptionKind::Cyclic && s_link.cyclicKey.groupId == grp.id;
    return RowMode{ s_deepMode, linked, s_linkPending, linked ? &s_link.cyclicKey.slotId : nullptr };
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawListToolbar
//--------------------------------------------------------------------------------
// The line above a list, laid out as in the old panel: the list's name (drop a
// row on it to uncategorize), "+" to add an item, then "Categories" and "+" to
// add a category. Each "+" greys with the WE_TIP_FINISH_NAMING tooltip while its
// last new entry is unresolved. Clicks come back through addItem and addCategory
// and are applied by the caller after the list has drawn, so no index moves mid-
// draw.
//--------------------------------------------------------------------------------
static void DrawListToolbar(const char* nameKey, const char* dragType, std::vector<Category>& categories, const NameEditState& edit,
    bool itemCreationPending, const char* addItemId, const char* addCategoryId, bool& addItem, bool& addCategory)
{
    ImGui::TextUnformatted(Tr(nameKey));
    MakeDropTarget(dragType, categories, -1); //. drop here to uncategorize

    ImGui::SameLine();
    DisabledBlock(itemCreationPending)
    {
        addItem = ImGui::SmallButton(addItemId);
    }
    if (itemCreationPending && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(Tr("WE_OPT_CATEGORIES"));
    ImGui::SameLine();
    DisabledBlock(edit.newKey >= 0)
    {
        addCategory = ImGui::SmallButton(addCategoryId);
    }
    if (edit.newKey >= 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddCategory
//--------------------------------------------------------------------------------
// Appends an unnamed category; its name box opens focused on the next draw and
// edit.newKey blocks the "+" until it is saved or cancelled. The search is
// cleared, since a query would hide the unnamed entry and leave the "+" locked.
//--------------------------------------------------------------------------------
static void AddCategory(std::vector<Category>& categories, NameEditState& edit)
{
    Category newCat;
    newCat.id = NewUniqueId("category", categories);
    categories.push_back(newCat); //. customName empty: name box opens
    edit.pendingFocus = (int)categories.size() - 1;
    edit.newKey       = edit.pendingFocus;
    s_searchBuf[0]    = '\0'; //. unnamed entry must stay visible
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddBasicEvent / AddCyclicGroup
//--------------------------------------------------------------------------------
// Append an unnamed entry with default values and ask its row to open the name
// box (RequestBasicEventNameEdit / RequestCyclicGroupNameEdit,
// options_events_rows.h). Both switch to Deep, where the new entry's fields are;
// both also clear the search, since a query would hide the unnamed entry and
// leave the "+" locked.
//--------------------------------------------------------------------------------
static void AddBasicEvent()
{
    WorldEvent newEvent{};
    newEvent.id         = NewUniqueId("event", g_Events);
    newEvent.customName = ""; //. unnamed: forces the name box
    newEvent.continentX = 49332.0f;
    newEvent.continentY = 31457.0f;
    newEvent.isVarying  = false;
    newEvent.duration   = 900;  //. 15 min, a reasonable default
    newEvent.period     = 7200; //. 2h, most common period
    newEvent.offset     = 0;
    g_Events.push_back(newEvent);
    RequestBasicEventNameEdit((int)g_Events.size() - 1);

    s_deepMode     = true;
    s_searchBuf[0] = '\0';
}

static void AddCyclicGroup()
{
    CyclicGroup newGroup{};
    newGroup.id         = NewUniqueId("group", g_CyclicGroups);
    newGroup.customName = ""; //. unnamed: forces the name box
    newGroup.continentX = 49332.0f;
    newGroup.continentY = 31457.0f;
    newGroup.period     = 7200; //. 2h, most common period
    newGroup.colors     = ColorSet{ ImVec4(0.502f, 0.502f, 0.502f, 1.0f) }; //. neutral gray, placeholder
    g_CyclicGroups.push_back(newGroup);
    RequestCyclicGroupNameEdit((int)g_CyclicGroups.size() - 1);

    s_deepMode     = true;
    s_searchBuf[0] = '\0';
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCategorizedList
//--------------------------------------------------------------------------------
// Draws items as one tree node per category with its members nested, then the
// uncategorized items. Item is any type with an id matching Category::members.
// drawRow(i) draws items[i] inside its own PushID(i), the only part that differs
// per tab and mode; it must defer any erase until this returns. Category nodes
// are DrawNameAndContextMenu rows (rename, right-click Delete) and drop targets
// for dragType. A query skips categories without a match and opens the ones that
// have one; the frame it clears, every category collapses again (wasSearchActive
// carries that edge, one per list). linkedId is the item a deep link wants shown;
// its category opens ahead of that collapse. A deleted category goes after the
// loops and its members become uncategorized.
//--------------------------------------------------------------------------------
template <typename Item, typename RowFn>
static void DrawCategorizedList(const std::vector<Item>& items, std::vector<Category>& categories, CategoryListKind kind, const char* dragType,
    NameEditState& edit, const std::string& queryLower, bool (*matches)(const Item&, const std::string&), bool& wasSearchActive,
    const std::string& linkedId, RowFn drawRow)
{
    const bool searchActive      = !queryLower.empty();
    const bool searchJustCleared = wasSearchActive && !searchActive;
    wasSearchActive = searchActive;

    std::vector<bool> isCategorized(items.size(), false);
    int pendingRemoveCategory = -1;

    for (int c = 0; c < (int)categories.size(); c++)
    {
        Category& cat = categories[c];
        ImGui::PushID(c);

        //_ Members resolved to item indices in member order; ids with no matching item are skipped.
        std::vector<int> memberIndices;
        for (const std::string& memberId : cat.members)
            for (int i = 0; i < (int)items.size(); i++)
                if (items[i].id == memberId) { memberIndices.push_back(i); break; }

        //_ A copy: the pointer DisplayName returns dies when a rename below rewrites customName.
        const std::string categoryName = DisplayName(cat, kind);
        const bool nameMatches = ContainsCaseInsensitive(categoryName, queryLower);
        const bool hasMatch = nameMatches || std::any_of(memberIndices.begin(), memberIndices.end(),
            [&](int i) { return matches(items[i], queryLower); });

        const bool holdsLinked = !linkedId.empty() && std::any_of(memberIndices.begin(), memberIndices.end(),
            [&](int i) { return items[i].id == linkedId; });

        bool open = false;
        if (!searchActive || hasMatch)
        {
            //_ Always, not Once: the state must be reapplied on the frame the query changes or a link lands.
            if (searchActive || holdsLinked)
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            else if (searchJustCleared)
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);

            NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, categoryName, edit, pendingRemoveCategory);
            open = nameResult.open;
            MakeDropTarget(dragType, categories, c);

            //_ Members and forced membership reference the category by id, never customName, so a rename needs no patching.
            if (nameResult.newName != categoryName)
                cat.customName = nameResult.newName;
        }

        //_ Marked even when the category is folded or skipped, so a member never resurfaces as uncategorized.
        for (int i : memberIndices)
        {
            isCategorized[i] = true;

            if (open && (nameMatches || matches(items[i], queryLower)))
            {
                ImGui::PushID(i);
                drawRow(i);
                ImGui::PopID();
            }
        }

        if (open)
            ImGui::TreePop();

        ImGui::PopID();
    }

    //_ Named scope, so these row IDs cannot collide with a category's PushID(c) above.
    ImGui::PushID("uncategorized");
    for (int i = 0; i < (int)items.size(); i++)
    {
        if (isCategorized[i]) continue;
        if (!matches(items[i], queryLower)) continue;

        ImGui::PushID(i);
        drawRow(i);
        ImGui::PopID();
    }
    ImGui::PopID();

    if (pendingRemoveCategory >= 0)
    {
        categories.erase(categories.begin() + pendingRemoveCategory);

        //_ Later categories moved down one slot, so their rename boxes and the new-entry marker follow them.
        std::map<int, std::string> shifted;
        for (const auto& [index, text] : edit.names)
        {
            if (index < pendingRemoveCategory)      shifted[index]     = text;
            else if (index > pendingRemoveCategory) shifted[index - 1] = text;
        }
        edit.names.swap(shifted);

        if (edit.newKey == pendingRemoveCategory)     edit.newKey = -1;
        else if (edit.newKey > pendingRemoveCategory) edit.newKey--;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicList   (pairs with: DrawCyclicList)
//--------------------------------------------------------------------------------
// The toolbar, then the list: DrawBasicEventRow (options_events_rows.h) draws
// every event, in Quick and Deep mode alike. A row's remove request and the
// toolbar's adds are applied after the loop, so no index moves mid-draw.
//--------------------------------------------------------------------------------
static void DrawBasicList(const std::string& queryLower)
{
    const std::string& linkedId = LinkedItemId(SubscriptionKind::Basic);

    bool addEvent    = false;
    bool addCategory = false;
    DrawListToolbar("WE_OPT_BASIC_EVENTS", kBasicEventDragType, g_BasicCategories, s_basicCategoryEdit, IsBasicEventCreationPending(),
        "+##add_basic_event", "+##add_basic_category", addEvent, addCategory);

    int pendingRemoveIndex = -1;
    DrawCategorizedList(g_Events, g_BasicCategories, CategoryListKind::Basic, kBasicEventDragType, s_basicCategoryEdit, queryLower,
        EventMatchesSearch, s_basicWasSearching, linkedId, [&](int i) { DrawBasicEventRow(i, BasicRowMode(g_Events[i]), pendingRemoveIndex); });

    if (pendingRemoveIndex >= 0)
        g_Events.erase(g_Events.begin() + pendingRemoveIndex);

    if (addEvent)
        AddBasicEvent();
    if (addCategory)
        AddCategory(g_BasicCategories, s_basicCategoryEdit);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicList   (pairs with: DrawBasicList)
//--------------------------------------------------------------------------------
// Same as DrawBasicList, with DrawCyclicGroupRow.
//--------------------------------------------------------------------------------
static void DrawCyclicList(const std::string& queryLower)
{
    const std::string& linkedId = LinkedItemId(SubscriptionKind::Cyclic);

    bool addGroup    = false;
    bool addCategory = false;
    DrawListToolbar("WE_OPT_CYCLIC_EVENTS", kCyclicGroupDragType, g_CyclicCategories, s_cyclicCategoryEdit, IsCyclicGroupCreationPending(),
        "+##add_cyclic_group", "+##add_cyclic_category", addGroup, addCategory);

    int pendingRemoveGroupIndex = -1;
    DrawCategorizedList(g_CyclicGroups, g_CyclicCategories, CategoryListKind::Cyclic, kCyclicGroupDragType, s_cyclicCategoryEdit, queryLower,
        GroupMatchesSearch, s_cyclicWasSearching, linkedId, [&](int i) { DrawCyclicGroupRow(i, GroupRowMode(g_CyclicGroups[i]), pendingRemoveGroupIndex); });

    if (pendingRemoveGroupIndex >= 0)
        g_CyclicGroups.erase(g_CyclicGroups.begin() + pendingRemoveGroupIndex);

    if (addGroup)
        AddCyclicGroup();
    if (addCategory)
        AddCategory(g_CyclicCategories, s_cyclicCategoryEdit);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetOptionsEventsView   (see: options_events.h)
//--------------------------------------------------------------------------------
void ResetOptionsEventsView()
{
    s_deepMode = false;
    s_searchBuf[0] = '\0';
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsEvents   (see: options_events.h)
//--------------------------------------------------------------------------------
void DrawOptionsEvents(const OptionsDeepLink* link)
{
    if (link)
    {
        s_searchBuf[0] = '\0'; //. row must stay visible
        if (ImGui::GetActiveID() == ImGui::GetID("##events_search"))
            ImGui::ClearActiveID(); //. focused box keeps own text

        if (link->kind == SubscriptionKind::Basic || link->kind == SubscriptionKind::Cyclic)
        {
            s_link        = *link;
            s_linkPending = true;
            s_linkFrame   = ImGui::GetFrameCount();
        }
    }
    else if (s_linkPending && ImGui::GetFrameCount() - s_linkFrame > kLinkLifeFrames)
    {
        s_linkPending = false;
    }

    DrawTopStrip();
    const std::string queryLower = SearchQueryLower();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_SHARED", kDrawSharedSettingsId).c_str()))
        DrawSharedSettings();

    if (ImGui::BeginTabBar("##events_subtabs"))
    {
        if (ImGui::BeginTabItem(TrId("WE_OPT_BASIC_EVENTS", "###events_tab_basic").c_str(), nullptr, SelectIfLinked(SubscriptionKind::Basic)))
        {
            if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_BASIC", kDrawBasicSettingsId).c_str()))
                DrawBasicSettings();

            DrawBasicList(queryLower);
            ConsumeLink(SubscriptionKind::Basic);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TrId("WE_OPT_CYCLIC_EVENTS", "###events_tab_cyclic").c_str(), nullptr, SelectIfLinked(SubscriptionKind::Cyclic)))
        {
            if (ImGui::CollapsingHeader(TrId("WE_OPTWIN_HDR_CYCLIC", kDrawCyclicSettingsId).c_str()))
                DrawCyclicSettings();

            DrawCyclicList(queryLower);
            ConsumeLink(SubscriptionKind::Cyclic);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
