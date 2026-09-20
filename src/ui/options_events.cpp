//################################################################################
// options_events.cpp   (see: options_events.h)
//--------------------------------------------------------------------------------
// kMinSearchWidthEm    narrowest the search box may shrink to
// kAlphaSwatchFlags    color swatch whose picker has an alpha bar
// kLinkLifeFrames      frames a deep link waits for its sub-tab to show
// kLinkFlashId         highlight id the linked row's flash runs on
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
// DrawSharedSettings   body of the Shared settings header
// DrawBasicSettings    body of the Basic event settings header
// DrawCyclicSettings   body of the Cyclic event settings header
// SearchQueryLower     search box text, lowercased for the predicates
// BeginLinkedRow       flash and landing test for one Quick row
// LinkedItemId/SelectIfLinked/ConsumeLink
//                      list-level side of a pending deep link
// DrawQuickBasicRow    Quick row for one Basic event
// DrawQuickSlotRow     Quick row for one Cyclic slot
// DrawQuickGroupRow    Quick row for one Cyclic group, nests its slot rows
// CategoryEditState    what one list's category rows remember between frames
// DrawListToolbar      line above a list: name, add item, add category
// AddCategory          append an unnamed category and open its name box
// AddBasicEvent/AddCyclicGroup
//                      append an unnamed event or group with default values
// DrawCategorizedList  the one category-aware list loop, takes a row renderer
// DrawBasicList        toolbar and event list under the Basic settings header
// DrawCyclicList       toolbar and group list under the Cyclic settings header
//--------------------------------------------------------------------------------

#include "options_events.h"

#include "addon_options_helpers.h" //. Tooltip, DisabledBlock, row drawers, search predicates
#include "events.h" //. g_Events, g_CyclicGroups
#include "events_categories.h" //. Category, DisplayName
#include "events_storage.h" //. DisplayName for events, groups and slots
#include "events_tracking.h" //. done-for-today queries and toggles
#include "imgui.h"
#include "localization.h"
#include "maprender.h" //. GetEventIconFilenames
#include "reset_defaults.h" //. DrawResetToDefaultsButton/Popup, DrawRestoreMissingButton
#include "settings.h"
#include "subscriptions.h" //. IsCyclicSlotSubscribed
#include "texture_whitener.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

//_ Floor for the search box width in multiples of the font size, so a narrow window never squeezes it to nothing.
static constexpr float kMinSearchWidthEm = 6.0f;

//_ Every color on this tab is RGBA.
static constexpr ImGuiColorEditFlags kAlphaSwatchFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;

//_ Covers the frame a sub-tab selection takes to show, plus one spare.
static constexpr int kLinkLifeFrames = 3;

//_ One id for every row: s_link decides which row matches it.
static constexpr const char* kLinkFlashId = "events_linked_row";

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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CategoryEditState
//--------------------------------------------------------------------------------
// editingNames  inline-rename text per category index; an entry exists while that
//               category's name box is open
// newIndex      category added but not yet saved or cancelled, else -1; blocks
//               the "+" button until resolved
// pendingFocus  category whose name box takes keyboard focus on its first draw,
//               else -1; consumed that frame
//--------------------------------------------------------------------------------
struct CategoryEditState
{
    std::map<int, std::string> editingNames;
    int newIndex     = -1;
    int pendingFocus = -1;
};

//_ Not persisted; one per list so a rename in progress on one tab survives a switch to the other.
static CategoryEditState s_basicCategoryEdit;
static CategoryEditState s_cyclicCategoryEdit;

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
// Lists GetEventIconFilenames() (maprender.h) after a leading None entry that
// stores an empty name. A stored name missing from the folder shows as None and
// stays untouched until another entry is picked. labelKey may be nullptr for a
// combo without a visible label.
//--------------------------------------------------------------------------------
static void DrawIconFileCombo(const char* labelKey, const char* id, std::string& filename)
{
    const std::vector<std::string>& files = GetEventIconFilenames();

    std::vector<const char*> labels;
    labels.push_back(Tr("WE_OPT_NONE"));
    for (const std::string& name : files)
        labels.push_back(name.c_str());

    int index = 0;
    for (int i = 0; i < (int)files.size(); i++)
    {
        if (files[i] == filename)
        {
            index = i + 1;
            break;
        }
    }

    const std::string label = labelKey ? TrId(labelKey, id) : std::string(id);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo(label.c_str(), &index, labels.data(), (int)labels.size()))
        filename = (index == 0) ? std::string() : files[index - 1];
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
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);

        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat(TrId("WE_OPT_START_GROWING_AT", "##basic_zoom_start_pct").c_str(), &BasicEventZoomStartPct, 1.0f, 0.0f, 100.0f, "%.0f%%");

        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat(TrId("WE_OPT_MAX_SIZE_AT_ZOOM", "##basic_zoom_max_mult").c_str(), &BasicEventZoomMaxMultiplier, 1.0f, 1.0f, 4.0f, "%.1fx");

        ImGui::Unindent(subToggleIndent);
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
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);

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
            ImGui::Indent(subToggleIndent);
            DrawIconFileCombo(nullptr, "##cyclic_hand_image_file", CyclicHandImageFilename);

            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_WIDTH", "##cyclic_hand_image_width").c_str(), &CyclicHandImageWidth, 1.0f, 2.0f, 60.0f, "%.0f px");
            Tooltip(Tr("WE_TIP_HAND_TEXTURE_WIDTH"));
            ImGui::Unindent(subToggleIndent);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_RING_EDGE_TEXTURE"));
        ImGui::Checkbox("##cyclic_ring_image_enabled", &CyclicRingImageEnabled);
        Tooltip(Tr("WE_TIP_RING_EDGE_TEXTURE"));

        DisabledBlock(!CyclicRingImageEnabled)
        {
            ImGui::SameLine();
            DrawIconFileCombo("WE_OPT_TEXTURE", "##cyclic_ring_image_file", CyclicRingImageFilename);

            ImGui::Indent(subToggleIndent);

            //_ The only control over the band's on-screen thickness.
            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_THICKNESS", "##cyclic_ring_image_thickness").c_str(), &CyclicRingImageThickness, 0.5f, 1.0f, 80.0f, "%.1f px");
            Tooltip(Tr("WE_TIP_RING_TEXTURE_THICKNESS"));

            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_OFFSET", "##cyclic_ring_image_offset").c_str(), &CyclicRingImageOffset, 0.1f, -5.0f, 5.0f, "%.1f px");
            Tooltip(Tr("WE_TIP_RING_TEXTURE_OFFSET"));
            ImGui::Unindent(subToggleIndent);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_OPT_FILL_TEXTURE"));
        ImGui::Checkbox("##cyclic_fill_image_enabled", &CyclicFillImageEnabled);
        Tooltip(Tr("WE_TIP_FILL_TEXTURE"));

        DisabledBlock(!CyclicFillImageEnabled)
        {
            ImGui::SameLine();
            DrawIconFileCombo(nullptr, "##cyclic_fill_image_file", CyclicFillImageFilename);

            ImGui::Indent(subToggleIndent);
            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat(TrId("WE_OPT_OPACITY", "##cyclic_fill_image_opacity").c_str(), &CyclicFillImageOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::Unindent(subToggleIndent);
        }

        ImGui::Unindent(subToggleIndent);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SearchQueryLower
//--------------------------------------------------------------------------------
// The form EventMatchesSearch and GroupMatchesSearch (addon_options_helpers.h)
// expect. Call after DrawTopStrip so the box's text from this frame is used.
//--------------------------------------------------------------------------------
static std::string SearchQueryLower()
{
    std::string query = s_searchBuf;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return query;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BeginLinkedRow
//--------------------------------------------------------------------------------
// Call before a Quick row's first item; isLinked says whether s_link names this
// row. Returns true on the one frame the pending link lands on it: the caller
// then scrolls to the row and opens its node, and the flash starts. While the
// flash runs (OptionsHighlight_Set, options_window.h), the row's first line gets
// a Header tint. It is drawn ahead of the row's items, so they sit on top of it.
//--------------------------------------------------------------------------------
static bool BeginLinkedRow(bool isLinked)
{
    if (!isLinked)
        return false;

    const bool landing = s_linkPending;
    if (landing)
        OptionsHighlight_Set(kLinkFlashId);

    if (OptionsHighlight_IsActive(kLinkFlashId))
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + ImGui::GetContentRegionAvail().x, pos.y + ImGui::GetFrameHeight());
        ImGui::GetWindowDrawList()->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetStyle().FrameRounding);
    }

    return landing;
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
// DrawQuickBasicRow/DrawQuickSlotRow/DrawQuickGroupRow
//--------------------------------------------------------------------------------
// The Quick renderers for DrawCategorizedList. Each row is a tree node; the
// collapsed line shows the state at a glance: notify icon, then the show-on-map
// checkbox, then the name (a group has the checkbox only). The expanded body of
// an event or slot is the four-way DrawNotifyLevelButtons jump plus "Done for
// today"; a group's is a "Subscribe all" checkbox above its slot rows. The icon
// and the buttons re-read the level, since the icon may change it this frame.
// Levels and done flags live in events_tracking.h and subscriptions.h. The row
// s_link names goes through BeginLinkedRow: it opens and scrolls into view. A
// link to one slot opens the slot's group and flashes the slot, not the group.
//--------------------------------------------------------------------------------
static void DrawQuickBasicRow(int i)
{
    WorldEvent& ev = g_Events[i];
    const bool landing = BeginLinkedRow(s_link.kind == SubscriptionKind::Basic && s_link.basicId == ev.id);

    int notifyLevel = GetBasicEventNotifyLevel(ev.id);
    int newNotifyLevel = DrawNotifyLevelIcon("##quick_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.id, newNotifyLevel);
    if (landing)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item
    ImGui::SameLine();

    DrawSubscribeCheckbox("##quick_show_on_map", ev.shown);
    Tooltip(Tr("WE_TIP_SHOW_ON_MAP"));
    ImGui::SameLine();

    //_ Always, not Once: a repeat link to the same row must reopen it.
    if (landing)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::TreeNode("##quick_event_node", "%s", DisplayName(ev)))
    {
        int level = GetBasicEventNotifyLevel(ev.id);
        int newLevel = DrawNotifyLevelButtons("##quick_notify_buttons", level);
        if (newLevel != level)
            SetBasicEventNotifyLevel(ev.id, newLevel);

        bool doneToday = IsBasicEventMarkedDoneToday(ev.id);
        if (ImGui::Checkbox(Tr("WE_OPTWIN_QUICK_DONE_TODAY"), &doneToday))
            ToggleBasicEventDoneToday(ev.id);

        ImGui::TreePop();
    }
}

static void DrawQuickSlotRow(CyclicGroup& grp, int s)
{
    CyclicGroup::Slot& slot = grp.slots[s];
    CyclicSubscriptionKey key{ grp.id, slot.id };
    const bool landing = BeginLinkedRow(s_link.kind == SubscriptionKind::Cyclic && s_link.cyclicKey == key);

    int notifyLevel = GetCyclicSlotNotifyLevel(key);
    int newNotifyLevel = DrawNotifyLevelIcon("##quick_notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetCyclicSlotNotifyLevel(key, newNotifyLevel);
    if (landing)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item
    ImGui::SameLine();

    DrawSubscribeCheckbox("##quick_show_slot_on_map", slot.shown);
    Tooltip(Tr("WE_TIP_SHOW_OCCURRENCE"));
    ImGui::SameLine();

    if (landing)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::TreeNode("##quick_slot_node", "%s", DisplayName(slot, grp.id)))
    {
        int level = GetCyclicSlotNotifyLevel(key);
        int newLevel = DrawNotifyLevelButtons("##quick_notify_buttons", level);
        if (newLevel != level)
            SetCyclicSlotNotifyLevel(key, newLevel);

        bool doneToday = IsCyclicSlotMarkedDoneToday(key);
        if (ImGui::Checkbox(Tr("WE_OPTWIN_QUICK_DONE_TODAY"), &doneToday))
            ToggleCyclicSlotDoneToday(key);

        ImGui::TreePop();
    }
}

static void DrawQuickGroupRow(int i)
{
    CyclicGroup& grp = g_CyclicGroups[i];
    const bool inLink = s_link.kind == SubscriptionKind::Cyclic && s_link.cyclicKey.groupId == grp.id;
    const bool landing = BeginLinkedRow(inLink && s_link.cyclicKey.slotId.empty());

    DrawSubscribeCheckbox("##quick_show_group_on_map", grp.shown);
    if (landing)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item
    Tooltip(Tr("WE_TIP_SHOW_RING"));
    ImGui::SameLine();

    if (inLink && s_linkPending)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::TreeNode("##quick_group_node", "%s", DisplayName(grp)))
    {
        bool allSlotsSubscribed = !grp.slots.empty() &&
            std::all_of(grp.slots.begin(), grp.slots.end(), [&](const CyclicGroup::Slot& slot)
            {
                return IsCyclicSlotSubscribed(CyclicSubscriptionKey{ grp.id, slot.id });
            });
        if (DrawSubscribeCheckbox("##quick_subscribe_group", allSlotsSubscribed))
        {
            for (const auto& slot : grp.slots)
            {
                CyclicSubscriptionKey key{ grp.id, slot.id };
                //_ Unticking drops every slot to 0; ticking only raises 0 to 1, so higher levels survive.
                if (!allSlotsSubscribed)
                    SetCyclicSlotNotifyLevel(key, 0);
                else if (GetCyclicSlotNotifyLevel(key) == 0)
                    SetCyclicSlotNotifyLevel(key, 1);
            }
        }
        Tooltip(Tr("WE_TIP_SUBSCRIBE_CYCLE"));
        ImGui::SameLine();
        ImGui::TextUnformatted(Tr("WE_OPTWIN_QUICK_SUBSCRIBE_ALL"));

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            ImGui::PushID(s);
            DrawQuickSlotRow(grp, s);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
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
static void DrawListToolbar(const char* nameKey, const char* dragType, std::vector<Category>& categories, const CategoryEditState& edit,
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
    DisabledBlock(edit.newIndex >= 0)
    {
        addCategory = ImGui::SmallButton(addCategoryId);
    }
    if (edit.newIndex >= 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddCategory
//--------------------------------------------------------------------------------
// Appends an unnamed category; its name box opens focused on the next draw and
// edit.newIndex blocks the "+" until it is saved or cancelled. The search is
// cleared, since a query would hide the unnamed entry and leave the "+" locked.
//--------------------------------------------------------------------------------
static void AddCategory(std::vector<Category>& categories, CategoryEditState& edit)
{
    std::unordered_set<std::string> usedIds;
    for (const Category& existing : categories) usedIds.insert(existing.id);

    Category newCat;
    //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
    newCat.id = UniqueId(SlugifyName("category"), usedIds);
    categories.push_back(newCat); //. customName empty: name box opens
    edit.pendingFocus = (int)categories.size() - 1;
    edit.newIndex     = edit.pendingFocus;
    s_searchBuf[0]    = '\0'; //. unnamed entry must stay visible
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddBasicEvent / AddCyclicGroup
//--------------------------------------------------------------------------------
// Append an unnamed entry with default values and ask its row to open the name
// box (RequestBasicEventNameEdit / RequestCyclicGroupNameEdit,
// addon_options_helpers.h). Only Deep rows draw that box, so both switch to Deep;
// both also clear the search, since a query would hide the unnamed entry and
// leave the "+" locked.
//--------------------------------------------------------------------------------
static void AddBasicEvent()
{
    std::unordered_set<std::string> usedIds;
    for (const WorldEvent& ev : g_Events) usedIds.insert(ev.id);

    WorldEvent newEvent{};
    //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
    newEvent.id         = UniqueId(SlugifyName("event"), usedIds);
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
    std::unordered_set<std::string> usedIds;
    for (const CyclicGroup& grp : g_CyclicGroups) usedIds.insert(grp.id);

    CyclicGroup newGroup{};
    //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
    newGroup.id         = UniqueId(SlugifyName("group"), usedIds);
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
    CategoryEditState& edit, const std::string& queryLower, bool (*matches)(const Item&, const std::string&), bool& wasSearchActive,
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

            const bool autoFocus = (edit.pendingFocus == c);
            if (autoFocus)
            {
                edit.editingNames[c] = ""; //. freshly created - starts empty, forces the inline editor open
                edit.pendingFocus = -1;
            }
            const bool isNew = (edit.newIndex == c);

            NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, categoryName, edit.editingNames, pendingRemoveCategory,
                nullptr, std::string(), nullptr, nullptr, -1, nullptr, nullptr, true, autoFocus, isNew);
            open = nameResult.open;
            MakeDropTarget(dragType, categories, c);

            //_ Members and forced membership reference the category by id, never customName, so a rename needs no patching.
            if (nameResult.newName != categoryName)
                cat.customName = nameResult.newName;

            //_ Resolved (saved or cancelled) - frees the "+" button back up.
            if (isNew && (nameResult.cancelled || nameResult.newName != categoryName))
                edit.newIndex = -1;
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
        for (const auto& [index, text] : edit.editingNames)
        {
            if (index < pendingRemoveCategory)      shifted[index]     = text;
            else if (index > pendingRemoveCategory) shifted[index - 1] = text;
        }
        edit.editingNames.swap(shifted);

        if (edit.newIndex == pendingRemoveCategory)     edit.newIndex = -1;
        else if (edit.newIndex > pendingRemoveCategory) edit.newIndex--;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicList   (pairs with: DrawCyclicList)
//--------------------------------------------------------------------------------
// The toolbar, then the list: Deep mode draws DrawBasicEventRow
// (addon_options_helpers.h) for every event, Quick mode DrawQuickBasicRow. A Deep
// row's remove request and the toolbar's adds are applied after the loop, so no
// index moves mid-draw.
//--------------------------------------------------------------------------------
static void DrawBasicList(const std::string& queryLower)
{
    const std::string& linkedId = LinkedItemId(SubscriptionKind::Basic);

    bool addEvent    = false;
    bool addCategory = false;
    DrawListToolbar("WE_OPT_BASIC_EVENTS", kBasicEventDragType, g_BasicCategories, s_basicCategoryEdit, IsBasicEventCreationPending(),
        "+##add_basic_event", "+##add_basic_category", addEvent, addCategory);

    if (!s_deepMode)
    {
        DrawCategorizedList(g_Events, g_BasicCategories, CategoryListKind::Basic, kBasicEventDragType, s_basicCategoryEdit, queryLower,
            EventMatchesSearch, s_basicWasSearching, linkedId, [](int i) { DrawQuickBasicRow(i); });
    }
    else
    {
        int pendingRemoveIndex = -1;
        DrawCategorizedList(g_Events, g_BasicCategories, CategoryListKind::Basic, kBasicEventDragType, s_basicCategoryEdit, queryLower,
            EventMatchesSearch, s_basicWasSearching, linkedId, [&](int i) { DrawBasicEventRow(i, pendingRemoveIndex); });

        if (pendingRemoveIndex >= 0)
            g_Events.erase(g_Events.begin() + pendingRemoveIndex);
    }

    if (addEvent)
        AddBasicEvent();
    if (addCategory)
        AddCategory(g_BasicCategories, s_basicCategoryEdit);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicList   (pairs with: DrawBasicList)
//--------------------------------------------------------------------------------
// Same as DrawBasicList, with DrawCyclicGroupRow (Deep) or DrawQuickGroupRow.
//--------------------------------------------------------------------------------
static void DrawCyclicList(const std::string& queryLower)
{
    const std::string& linkedId = LinkedItemId(SubscriptionKind::Cyclic);

    bool addGroup    = false;
    bool addCategory = false;
    DrawListToolbar("WE_OPT_CYCLIC_EVENTS", kCyclicGroupDragType, g_CyclicCategories, s_cyclicCategoryEdit, IsCyclicGroupCreationPending(),
        "+##add_cyclic_group", "+##add_cyclic_category", addGroup, addCategory);

    if (!s_deepMode)
    {
        DrawCategorizedList(g_CyclicGroups, g_CyclicCategories, CategoryListKind::Cyclic, kCyclicGroupDragType, s_cyclicCategoryEdit, queryLower,
            GroupMatchesSearch, s_cyclicWasSearching, linkedId, [](int i) { DrawQuickGroupRow(i); });
    }
    else
    {
        int pendingRemoveGroupIndex = -1;
        DrawCategorizedList(g_CyclicGroups, g_CyclicCategories, CategoryListKind::Cyclic, kCyclicGroupDragType, s_cyclicCategoryEdit, queryLower,
            GroupMatchesSearch, s_cyclicWasSearching, linkedId, [&](int i) { DrawCyclicGroupRow(i, pendingRemoveGroupIndex); });

        if (pendingRemoveGroupIndex >= 0)
            g_CyclicGroups.erase(g_CyclicGroups.begin() + pendingRemoveGroupIndex);
    }

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
        ResetOptionsEventsView(); //. row must stay visible
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

    if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_SHARED")))
        DrawSharedSettings();

    if (ImGui::BeginTabBar("##events_subtabs"))
    {
        if (ImGui::BeginTabItem(TrId("WE_OPT_BASIC_EVENTS", "###events_tab_basic").c_str(), nullptr, SelectIfLinked(SubscriptionKind::Basic)))
        {
            if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_BASIC")))
                DrawBasicSettings();

            DrawBasicList(queryLower);
            ConsumeLink(SubscriptionKind::Basic);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TrId("WE_OPT_CYCLIC_EVENTS", "###events_tab_cyclic").c_str(), nullptr, SelectIfLinked(SubscriptionKind::Cyclic)))
        {
            if (ImGui::CollapsingHeader(Tr("WE_OPTWIN_HDR_CYCLIC")))
                DrawCyclicSettings();

            DrawCyclicList(queryLower);
            ConsumeLink(SubscriptionKind::Cyclic);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
