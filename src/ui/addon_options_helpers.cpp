//################################################################################
// addon_options_helpers.cpp
//--------------------------------------------------------------------------------
// Implementations for everything declared in addon_options_helpers.h - the
// scoped-disable helper, period widget, bulk icon picker, color conversion,
// duplicate-name checks, drag-and-drop plumbing, hand-drawn glyphs, the notify-
// level control, the shared name/context-menu row, search predicates, and the two
// full row drawers (Basic Event / Cyclic Group). See addon_options.cpp for the
// panel that assembles these into the actual Nexus options UI.
//--------------------------------------------------------------------------------

#include "addon_options_helpers.h"
#include "better_chat.h" //. IsBetterChatSelfCommandEnabled, for BuildChatChannelOptions
#include "color_utils.h"
#include "events_storage.h" //. GetDefaultEvent/GetDefaultCyclicGroup/GetDefaultCyclicSlot/DisplayName
#include "events_tracking.h"
#include "imgui_internal.h" //. for internal-only ImGui APIs
#include "localization.h"
#include "subscriptions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_set>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ImGuiScopedDisabled ctor / dtor
//--------------------------------------------------------------------------------
// See the struct's own comment in the header for the disable/dim contract.
//--------------------------------------------------------------------------------
ImGuiScopedDisabled::ImGuiScopedDisabled(bool cond) : active(cond)
{
    if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
}

ImGuiScopedDisabled::~ImGuiScopedDisabled()
{
    if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PeriodSecondsToHours / DrawPeriodHoursDragInt
//--------------------------------------------------------------------------------
// Whole hours only, 1-12h: no GW2 event/chain runs on anything but a whole-hour
// cycle. A DragInt is used instead of a Combo over a fixed label array, leaving
// room to raise the cap later without code changes.
//
// PeriodSecondsToHours clamps to [kMinPeriodHours, kMaxPeriodHours], snapping any
// out-of-range or non-whole-hour value (e.g. from a hand-edited JSON file) to the
// nearest valid hour.
//--------------------------------------------------------------------------------
int PeriodSecondsToHours(int periodSeconds)
{
    int hours = periodSeconds / 3600;
    if (hours < kMinPeriodHours) hours = kMinPeriodHours;
    if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
    return hours;
}

void DrawPeriodHoursDragInt(int* periodSeconds)
{
    int hours = PeriodSecondsToHours(*periodSeconds);
    if (ImGui::DragInt(Tr("WE_PERIOD_LABEL"), &hours, 0.1f, kMinPeriodHours, kMaxPeriodHours, "%dh"))
    {
        //_ DragInt's min/max only clamp the drag gesture; a typed (ctrl+click) value can still land outside range, so clamp explicitly.
        if (hours < kMinPeriodHours) hours = kMinPeriodHours;
        if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
        *periodSeconds = hours * 3600;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBulkIconPicker
//--------------------------------------------------------------------------------
// Display state before the user touches it: if every target already shares the
// exact same iconTexture (including "all empty", i.e. all using the plain dot),
// that shared value is shown selected. If they disagree, a "(mixed)" entry is
// shown instead - purely a status display, not a real choice: selecting any OTHER
// entry applies that choice to every target, and "(mixed)" naturally drops out of
// the list once the state resolves to non-mixed.
//--------------------------------------------------------------------------------
void DrawBulkIconPicker(const char* label, const std::vector<int>& targetIndices)
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
// IsDuplicateEventName / IsDuplicateGroupName / IsDuplicateSlotKey / DrawDuplicateWarning
//--------------------------------------------------------------------------------
// Display-only warning: flags two entries sharing a visible name, so the player
// can tell them apart in the UI. Not a merge-key check - GroupKey/EventKey/
// SlotKey (events_storage.cpp) key on id, not name, so a duplicate name here
// doesn't mean a duplicate identity. IsDuplicateEventName compares DisplayName
// (events_storage.h), i.e. the resolved/localized text, not the raw customName -
// two events with different customName can still collide once one falls through
// to a compiled default's translation. For slots this means unique WITHIN the
// group, not globally. selfIndex excludes the entry being checked from its own
// comparison.
//--------------------------------------------------------------------------------
bool IsDuplicateEventName(const std::vector<WorldEvent>& events, int selfIndex)
{
    std::string name = DisplayName(events[selfIndex]);
    if (name.empty()) return false;
    for (int i = 0; i < (int)events.size(); i++)
        if (i != selfIndex && DisplayName(events[i]) == name)
            return true;
    return false;
}

bool IsDuplicateGroupName(const std::vector<CyclicGroup>& groups, int selfIndex)
{
    std::string name = DisplayName(groups[selfIndex]);
    if (name.empty()) return false;
    for (int i = 0; i < (int)groups.size(); i++)
        if (i != selfIndex && DisplayName(groups[i]) == name)
            return true;
    return false;
}

bool IsDuplicateSlotKey(const std::vector<CyclicGroup::Slot>& slots, int selfIndex, const std::string& groupId)
{
    std::string name = DisplayName(slots[selfIndex], groupId);
    if (name.empty()) return false;
    for (int i = 0; i < (int)slots.size(); i++)
        if (i != selfIndex && DisplayName(slots[i], groupId) == name)
            return true;
    return false;
}

void DrawDuplicateWarning()
{
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", Tr("WE_DUPLICATE_TAG"));
}

//********************************************************************************
// DragPayload
//--------------------------------------------------------------------------------
// id   fixed-size copy of the dragged item's id
//--------------------------------------------------------------------------------
// SetDragDropPayload copies a fixed-size raw blob - it has no idea about
// std::string, so the payload is a small POD struct with a fixed char[] buffer,
// matching the same nameBuf convention already used throughout this file for
// ImGui::InputText.
//--------------------------------------------------------------------------------
struct DragPayload
{
    char id[128];
};

//_ Two distinct types, not one with a discriminator - AcceptDragDropPayload filters by type, rejecting cross-list drops for free.
const char* const kBasicEventDragType  = "WE_DRAG_BASIC_EVENT";
const char* const kCyclicGroupDragType = "WE_DRAG_CYCLIC_GROUP";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MakeDragSource / MakeDropTarget
//--------------------------------------------------------------------------------
// MakeDragSource: call right after the widget being dragged (e.g. a row's
// TreeNode). itemId is the payload moved into Category::members on drop;
// displayName is only the text shown under the cursor while dragging.
//
// MakeDropTarget: call right after the widget accepting a drop (a category
// header, or "drop here to uncategorize"). Performs the MoveCategoryMember() call
// itself; the bool return is informational.
//--------------------------------------------------------------------------------
void MakeDragSource(const char* dragType, const std::string& itemId, const std::string& displayName)
{
    if (ImGui::BeginDragDropSource())
    {
        DragPayload payload{};
        strncpy(payload.id, itemId.c_str(), sizeof(payload.id) - 1);
        ImGui::SetDragDropPayload(dragType, &payload, sizeof(payload));
        ImGui::TextUnformatted(displayName.c_str()); //. preview text following the cursor
        ImGui::EndDragDropSource();
    }
}

bool MakeDropTarget(const char* dragType, std::vector<Category>& categories, int targetCategoryIndex)
{
    bool dropped = false;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload(dragType))
        {
            const DragPayload* payload = (const DragPayload*)imguiPayload->Data;
            MoveCategoryMember(categories, std::string(payload->id), targetCategoryIndex);
            dropped = true;
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribeCheckbox
//--------------------------------------------------------------------------------
// Meant to sit immediately before a TreeNode call, on the same line (SameLine),
// producing "[x] > TreeNode". A plain ImGui::Checkbox is noticeably taller than a
// TreeNode arrow, so FramePadding is zeroed just for this one call to match the
// arrow's height. Returns true if toggled this frame, same contract as
// ImGui::Checkbox itself.
//--------------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleVar();
    return changed;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBellIcon / DrawSpeakerIcon
//--------------------------------------------------------------------------------
// Hand-drawn glyphs via ImDrawList primitives - no bell/speaker glyph in the base
// font. `center` is the visual center, `size` roughly the full height in pixels;
// both authored in the same 24-unit box (s = size/24). DrawSpeakerIcon doubles as
// level 3's icon below and as a standalone label next to the sound-file picker.
//--------------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; //. 24-unit authoring box
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    //_ Dome + flare: a semicircle over the top, then straight lines flaring to the rim; filled solid, not stroked.
    dl->PathArcTo(P(12.0f, 14.0f), 6.0f * s, IM_PI, IM_PI * 2.0f, 12);
    dl->PathLineTo(P(20.0f, 18.0f));
    dl->PathLineTo(P(4.0f, 18.0f));
    dl->PathFillConvex(color);

    dl->AddCircleFilled(P(12.0f, 20.4f), 1.3f * s, color, 12); //. clapper, not just a dome
}

void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; //. same box as DrawBellIcon
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    dl->AddRectFilled(P(5.0f, 9.0f), P(11.5f, 15.0f), color); //. housing, overlaps cone

    //_ Cone/flare drawn as its own convex trapezoid since the combined housing+cone silhouette isn't convex.
    dl->PathLineTo(P(11.0f, 9.0f));
    dl->PathLineTo(P(16.0f, 4.0f));
    dl->PathLineTo(P(16.0f, 20.0f));
    dl->PathLineTo(P(11.0f, 15.0f));
    dl->PathFillConvex(color);

    //_ Sound waves: two concentric arcs, stroked; a filled crescent this small would look like a smudge, not a wave.
    dl->PathArcTo(P(11.0f, 12.0f), 5.0f * s, -0.65f, 0.65f, 8);
    dl->PathStroke(color, false, 1.4f * s);

    dl->PathArcTo(P(11.0f, 12.0f), 8.5f * s, -0.55f, 0.55f, 8);
    dl->PathStroke(color, false, 1.4f * s);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelIcon
//--------------------------------------------------------------------------------
// Manual hit-test + ImDrawList (not a real widget), sized to GetFrameHeight() to
// match the tree arrow. Returns the level to apply this frame - unchanged unless
// this click just advanced it.
//--------------------------------------------------------------------------------
int DrawNotifyLevelIcon(const char* idSuffix, int level)
{
    ImGui::PushID(idSuffix);

    //_ Same reasoning as DrawSubscribeCheckbox: zero FramePadding so this icon matches the tree arrow's height instead of a full frame.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    float sq = ImGui::GetFrameHeight();
    ImVec2 rmin = ImGui::GetCursorScreenPos();
    ImVec2 rmax(rmin.x + sq, rmin.y + sq);
    ImVec2 center((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);

    bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rmin, rmax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered)
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    //_ Small pad keeps the +/- lines running edge-to-edge, matching the bell/speaker icons below.
    float pad = sq * 0.10f;

    switch (level)
    {
        case 0: //. unsubscribed - minus only
            dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
            break;
        case 1: //. subscribed, silent - plus
            dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
            dl->AddLine(ImVec2(center.x, rmin.y + pad), ImVec2(center.x, rmax.y - pad), col, 1.6f);
            break;
        case 2: //. subscribed + toast - bell
            DrawBellIcon(dl, center, sq * 0.96f, col);
            break;
        //_ Level 3 (subscribed + toast + sound) draws the speaker; clicking wraps back to level 0 (fully unsubscribed).
        default:
            DrawSpeakerIcon(dl, center, sq * 0.96f, col);
            break;
    }

    ImGui::Dummy(ImVec2(sq, sq));
    ImGui::PopStyleVar();

    int newLevel = level;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        newLevel = (level + 1) % 4;

    if (hovered)
    {
        ImGui::SetTooltip("%s",
            level == 0 ? Tr("WE_TIP_NOTIFY_ICON_LVL0") :
            level == 1 ? Tr("WE_TIP_NOTIFY_ICON_LVL1") :
            level == 2 ? Tr("WE_TIP_NOTIFY_ICON_LVL2")
                       : Tr("WE_TIP_NOTIFY_ICON_LVL3"));
    }

    ImGui::PopID();
    return newLevel;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelButtons   (pairs with: DrawNotifyLevelIcon)
//--------------------------------------------------------------------------------
// Four boxes, same authoring box/primitives as DrawNotifyLevelIcon. Glyphs are
// minus / plus / bell / speaker for levels 0-3 respectively - each level gets its
// own distinct glyph (a prior version doubled up the speaker on levels 2 and 3,
// relying on position + tooltip to disambiguate; that didn't hold up in testing).
// Each box is its own hit-test, clicking one jumps directly to that level; no
// wraparound, no cycle.
//--------------------------------------------------------------------------------
int DrawNotifyLevelButtons(const char* idSuffix, int level)
{
    ImGui::PushID(idSuffix);

    float sq = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    static const char* const kTooltipIds[4] = {
        "WE_TIP_NOTIFY_BTN_LVL0",
        "WE_TIP_NOTIFY_BTN_LVL1",
        "WE_TIP_NOTIFY_BTN_LVL2",
        "WE_TIP_NOTIFY_BTN_LVL3"
    };

    int newLevel = level;

    for (int lvl = 0; lvl < 4; lvl++)
    {
        if (lvl > 0)
            ImGui::SameLine(0.0f, 4.0f);

        ImGui::PushID(lvl);

        ImVec2 rmin = ImGui::GetCursorScreenPos();
        ImVec2 rmax(rmin.x + sq, rmin.y + sq);
        ImVec2 center((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);

        bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rmin, rmax);
        bool active  = (level == lvl);

        //_ Active box gets a filled+outlined frame; a merely-hovered inactive box gets just the hover fill.
        if (active)
        {
            dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_Header), 3.0f);
            dl->AddRect(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderActive), 3.0f, 0, 1.5f);
        }
        else if (hovered)
        {
            dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 3.0f);
        }

        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        float pad = sq * 0.10f;

        switch (lvl)
        {
            case 0: //. unsubscribed - minus only
                dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
                break;
            case 1: //. subscribed, silent - plus
                dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
                dl->AddLine(ImVec2(center.x, rmin.y + pad), ImVec2(center.x, rmax.y - pad), col, 1.6f);
                break;
            case 2: //. +toast - bell
                DrawBellIcon(dl, center, sq * 0.9f, col);
                break;
            default: //. lvl 3 - +toast+sound - speaker
                DrawSpeakerIcon(dl, center, sq * 0.9f, col);
                break;
        }

        ImGui::Dummy(ImVec2(sq, sq));

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            newLevel = lvl;

        if (hovered)
            ImGui::SetTooltip("%s", Tr(kTooltipIds[lvl]));

        ImGui::PopID();
    }

    ImGui::PopID();
    return newLevel;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDragButton
//--------------------------------------------------------------------------------
// Small button placed next to the Location field that arms/disarms map-drag edit
// mode for one Basic Event or Cyclic Group (see EditModeState in maprender.h).
// Reads "Drag" when this row isn't the one currently being edited, and "Stop"
// when it is - clicking it toggles. A hovered tooltip explains the interaction
// either way, since "Drag"/"Stop" alone doesn't say WHERE to actually drag it
// (the marker on the map, not this button).
//--------------------------------------------------------------------------------
void DrawDragButton(EditTarget target, int index, const char* idSuffix)
{
    bool isBeingEdited = (g_EditMode.target == target && g_EditMode.index == index);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s##drag_btn_%s", isBeingEdited ? "Stop" : "Drag", idSuffix);

    if (ImGui::SmallButton(buf))
    {
        if (isBeingEdited)
            ClearEditMode();
        else
            g_EditMode = { target, index };
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(isBeingEdited ? Tr("WE_TIP_DRAG_STOP") : Tr("WE_TIP_DRAG_START"));
        ImGui::EndTooltip();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BuildChatChannelOptions
//--------------------------------------------------------------------------------
// Index 0 is the empty prefix (ChatChannelPrefix's "current chat" default); the
// Better Chat entry stays last so dropping it is a single tail check. It's
// dropped unless Better Chat is loaded and reports its /self command enabled (see
// better_chat.h) - offering it otherwise would let the user pick a channel that
// just types "/self ..." into whatever chat box has focus.
//--------------------------------------------------------------------------------
void BuildChatChannelOptions(std::vector<const char*>& labels, std::vector<const char*>& prefixes)
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
// DrawNameAndContextMenu
//--------------------------------------------------------------------------------
// toggleDone, notifyLevel/setNotifyLevel, and resetToDefault add optional right-
// click entries, left null/-1 where not applicable (e.g. categories pass none of
// them). resetToDefault, when non-null, adds "Reset" between Edit name and
// Delete; resetAvailable greys it out for entries with no compiled-in default
// (see GetDefaultEvent/GetDefaultCyclicGroup/GetDefaultCyclicSlot in
// events_storage.h) instead of hiding the entry. editBuffers is the caller's own
// edit-in-progress map, keyed by editKey (separate from removeIndex since slots
// share one map across groups - see DrawCyclicGroupRow). Delete and Cancel both
// erase the editBuffers entry and set pendingRemoveIndex = removeIndex; Cancel
// (isNew only) additionally returns cancelled = true.
//--------------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(
    const char*                 treeNodeId,
    int                         editKey,
    int                         removeIndex,
    const std::string&          currentName,
    std::map<int, std::string>& editBuffers,
    int&                        pendingRemoveIndex,
    const char*                 dragType,
    const std::string&          dragId,
    const char*                 autoTag,
    std::function<void()>       toggleDone,
    int                         notifyLevel,
    std::function<void(int)>    setNotifyLevel,
    std::function<void()>       resetToDefault,
    bool                        resetAvailable,
    bool                        autoFocus,
    bool                        isNew)
{
    std::string label = currentName.empty() ? Tr("WE_UNNAMED") : currentName;
    if (autoTag)
    {
        label += " ";
        label += autoTag; //. display-only
    }
    bool open = ImGui::TreeNode(treeNodeId, "%s", label.c_str());
    if (autoTag && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_AUTO_TRACKED"));

    //_ Drag source is optional; categories are drop targets only and pass dragType = nullptr to skip it.
    if (dragType)
        MakeDragSource(dragType, dragId, currentName);

    if (ImGui::BeginPopupContextItem("##name_context_menu"))
    {
        if (toggleDone)
        {
            if (ImGui::MenuItem(Tr("WE_SUBS_MARK_DONE_TODAY")))
                toggleDone();
            ImGui::Separator();
        }
        if (setNotifyLevel && notifyLevel >= 0)
        {
            //_ Jump menu, not just a shortcut past the forward-only cycle; current stage shows a checkmark.
            if (ImGui::MenuItem(Tr("WE_ROW_NOTIFY_SUB_TOAST_SOUND"), nullptr, notifyLevel == 3))
                setNotifyLevel(3);
            if (ImGui::MenuItem(Tr("WE_ROW_NOTIFY_SUB_TOAST"), nullptr, notifyLevel == 2))
                setNotifyLevel(2);
            if (ImGui::MenuItem(Tr("WE_ROW_NOTIFY_SUB_ONLY"), nullptr, notifyLevel == 1))
                setNotifyLevel(1);
            if (ImGui::MenuItem(Tr("WE_ROW_NOTIFY_UNSUBSCRIBED"), nullptr, notifyLevel == 0))
                setNotifyLevel(0);
            ImGui::Separator();
        }
        if (ImGui::MenuItem(Tr("WE_ROW_EDIT_NAME")))
            editBuffers[editKey] = currentName; //. seeded when edit starts
        ImGui::Separator();
        if (resetToDefault)
        {
            if (ImGui::MenuItem(Tr("WE_ROW_RESET"), nullptr, false, resetAvailable))
                resetToDefault();
            ImGui::Separator();
        }
        if (ImGui::MenuItem(Tr("WE_ROW_DELETE")))
            pendingRemoveIndex = removeIndex;
        ImGui::EndPopup();
    }

    auto it = editBuffers.find(editKey);
    if (it == editBuffers.end())
        return { open, currentName };

    ImGui::SameLine();
    char buf[128];
    strncpy(buf, it->second.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (autoFocus)
        ImGui::SetKeyboardFocusHere();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("##inline_name_edit", buf, sizeof(buf)))
        it->second = buf; //. persists into next frame

    //_ Whitespace-only counts as blank - a name of all spaces would look identical to (unnamed) anyway.
    bool blank = it->second.find_first_not_of(" \t") == std::string::npos;

    ImGui::SameLine();
    DisabledBlock(blank)
    {
        if (ImGui::SmallButton(TrId("WE_ROW_SAVE", "##name_edit_save").c_str()))
        {
            std::string saved = it->second;
            editBuffers.erase(it);
            return { open, saved, false };
        }
    }
    if (blank && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_ROW_NAME_REQUIRED_TIP"));

    //_ Only a freshly-created, never-saved entry can be discarded outright
    if (isNew)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("x##name_edit_cancel"))
        {
            editBuffers.erase(it);
            pendingRemoveIndex = removeIndex;
            return { open, currentName, true };
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("WE_TIP_CANCEL_NEW"));
    }

    return { open, currentName, false }; //. unchanged until Save/Cancel is clicked
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContainsCaseInsensitive / EventMatchesSearch / GroupMatchesSearch
//--------------------------------------------------------------------------------
// One shared query filters both Basic Events and Cyclic Events at once - a single
// search box, not two.
//
// Matching is case-insensitive substring, and for Cyclic Events checks BOTH the
// group's own name AND every one of its slot names - so typing "Crash Site" finds
// Dry Top even though "Dry Top" itself doesn't contain that text.
//--------------------------------------------------------------------------------
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
{
    if (needleLower.empty()) return true; //. empty query matches everything
    std::string haystackLower = haystack;
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return haystackLower.find(needleLower) != std::string::npos;
}

bool EventMatchesSearch(const WorldEvent& ev, const std::string& queryLower)
{
    return ContainsCaseInsensitive(DisplayName(ev), queryLower);
}

bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower)
{
    if (ContainsCaseInsensitive(DisplayName(grp), queryLower)) return true;
    for (const auto& slot : grp.slots)
        if (ContainsCaseInsensitive(DisplayName(slot, grp.id), queryLower)) return true;
    return false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestBasicEventNameEdit / RequestCyclicGroupNameEdit   (see: addon_options_helpers.h)
//--------------------------------------------------------------------------------
// s_pending* is one-shot, consumed (and cleared) by DrawBasicEventRow/
// DrawCyclicGroupRow the next time that index draws - see each function's own
// editingNames seeding. s_new* is set alongside it but persists until
// DrawBasicEventRow/DrawCyclicGroupRow clears it (entry saved or cancelled) - see
// IsBasicEventCreationPending/IsCyclicGroupCreationPending.
//--------------------------------------------------------------------------------
static int s_pendingBasicEventEdit  = -1;
static int s_pendingCyclicGroupEdit = -1;
static int s_newBasicEventIndex     = -1;
static int s_newCyclicGroupIndex    = -1;

void RequestBasicEventNameEdit(int index)
{
    s_pendingBasicEventEdit = index;
    s_newBasicEventIndex    = index;
}

void RequestCyclicGroupNameEdit(int index)
{
    s_pendingCyclicGroupEdit = index;
    s_newCyclicGroupIndex    = index;
}

bool IsBasicEventCreationPending()  { return s_newBasicEventIndex  >= 0; }
bool IsCyclicGroupCreationPending() { return s_newCyclicGroupIndex >= 0; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicEventRow   (pairs with: DrawCyclicGroupRow)
//--------------------------------------------------------------------------------
// Draws one g_Events[i] row in full - extracted out of the main loop so both the
// "uncategorized" pass and each category's "members" pass can call the same
// drawing code. PushID/PopID is the CALLER's responsibility (the same index is
// drawn from different places depending on category membership). Sets
// pendingRemoveIndex = i on remove; does not modify g_Events directly.
//--------------------------------------------------------------------------------
void DrawBasicEventRow(int i, int& pendingRemoveIndex)
{
    //_ Tracks in-edit-mode indices (see DrawNameAndContextMenu); function-static, shared across category and uncategorized passes.
    static std::map<int, std::string> editingNames;

    WorldEvent& ev = g_Events[i];

    //_ Drawn before the name/tree-arrow, in the slot DrawSubscribeCheckbox used to occupy; see subscriptions.h for what each level touches.
    int notifyLevel = GetBasicEventNotifyLevel(ev.id);
    int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.id, newNotifyLevel);
    ImGui::SameLine();

    //_ Map-only show/hide; the Subscriptions bar/window are unaffected (that's the checkbox above). ev.shown defaults to true.
    DrawSubscribeCheckbox("##show_on_map", ev.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_ON_MAP"));
    ImGui::SameLine();

    std::string oldName = DisplayName(ev);
    const WorldEvent* defaultEv = GetDefaultEvent(ev.id);

    bool autoFocus = (s_pendingBasicEventEdit == i);
    if (autoFocus)
    {
        editingNames[i] = ""; //. freshly created - starts empty, forces the inline editor open
        s_pendingBasicEventEdit = -1;
    }
    bool isNew = (s_newBasicEventIndex == i);

    NameRowResult nameResult = DrawNameAndContextMenu("##event_node", i, i, DisplayName(ev), editingNames, pendingRemoveIndex, kBasicEventDragType, ev.id,
        ev.apiWorldBossId.empty() ? nullptr : "(auto)",
        [&ev]() { ToggleBasicEventDoneToday(ev.id); },
        notifyLevel, [&ev](int lvl) { SetBasicEventNotifyLevel(ev.id, lvl); },
        [&ev, defaultEv]() { if (defaultEv) ev = *defaultEv; }, //. customName cleared for free - defaultEv's own customName is always ""
        defaultEv != nullptr, autoFocus, isNew);
    bool open = nameResult.open;
    if (nameResult.newName != oldName)
    {
        ev.customName = nameResult.newName;
    }
    //_ Resolved (saved or cancelled) - frees the "+" button back up.
    if (isNew && (nameResult.cancelled || nameResult.newName != oldName))
        s_newBasicEventIndex = -1;

    if (IsDuplicateEventName(g_Events, i))
        DrawDuplicateWarning();

    if (open)
    {
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputFloat2(Tr("WE_LOCATION_LABEL"), &ev.continentX, "%.0f");

        ImGui::SameLine();
        {
            char idSuffix[16];
            snprintf(idSuffix, sizeof(idSuffix), "be%d", i);
            DrawDragButton(EditTarget::BasicEvent, i, idSuffix);
        }

        ImGui::SetNextItemWidth(50.0f);
        int durationMinutes = ev.duration / 60;
        if (ImGui::InputInt(Tr("WE_DURATION_MIN_LABEL"), &durationMinutes,0,0))
        {
            if (durationMinutes < 1) durationMinutes = 1;
            ev.duration = durationMinutes * 60;
        }

        ImGui::SameLine();
        ImGui::Checkbox(Tr("WE_VARYING_CHECKBOX"), &ev.isVarying);

        if (ev.isVarying)
        {
            //_ Sorted HH:MM start times, labeled UTC and not auto-converted; the schedule is UTC by design.
            ImGui::Spacing();
            ImGui::TextUnformatted(Tr("WE_TIMES_UTC_LABEL"));
            ImGui::SameLine();
            bool pendingAddTime = ImGui::SmallButton("+##add_time");

            int pendingRemoveTimeIndex = -1;

            for (int t = 0; t < (int)ev.varyingTimes.size(); t++)
            {
                ImGui::PushID(t);

                int hour   = ev.varyingTimes[t] / 3600;
                int minute = (ev.varyingTimes[t] % 3600) / 60;

                //_ Narrow, unlabeled fields (":" between them reads as a clock) so hour+minute+remove fit on one row.
                bool changed = false;
                ImGui::SetNextItemWidth(25.0f);
                if (ImGui::InputInt("##Hour", &hour, 0, 0))
                {
                    hour = std::clamp(hour, 0, 23);
                    changed = true;
                }
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextUnformatted(":");
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::SetNextItemWidth(25.0f);
                if (ImGui::InputInt("##Minute", &minute, 0, 0))
                {
                    minute = std::clamp(minute, 0, 59);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-##remove_time"))
                    pendingRemoveTimeIndex = t;

                if (changed)
                    ev.varyingTimes[t] = hour * 3600 + minute * 60;

                ImGui::PopID();
            }

            if (pendingRemoveTimeIndex >= 0)
                ev.varyingTimes.erase(ev.varyingTimes.begin() + pendingRemoveTimeIndex);

            if (pendingAddTime)
                ev.varyingTimes.push_back(0); //. midnight UTC

            //_ Re-sorted every frame (not conditionally) since GetSecondsUntilEventStart() requires ascending order.
            std::sort(ev.varyingTimes.begin(), ev.varyingTimes.end());
        }
        else
        {
            ImGui::SetNextItemWidth(50.0f);
            int offsetMinutes = ev.offset / 60;
            if (ImGui::InputInt(Tr("WE_OFFSET_MIN_LABEL"), &offsetMinutes, 0, 0))
            {
                if (offsetMinutes < 0) offsetMinutes = 0;
                ev.offset = offsetMinutes * 60;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(50.0f);
            DrawPeriodHoursDragInt(&ev.period);
        }

        //_ "Dot" (index 0) keeps the plain circle; any other entry names a textures/ file tinted to the status color (see maprender.cpp).
        const std::vector<std::string>& iconFiles = GetEventIconFilenames();
        std::vector<const char*> iconLabels;
        iconLabels.push_back(Tr("WE_ICON_DOT"));
        for (const auto& fn : iconFiles)
            iconLabels.push_back(fn.c_str());

        int iconIndex = 0; //. "Dot"
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == ev.iconTexture)
                iconIndex = k + 1;

        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo(TrId("WE_ICON_LABEL", "##event_icon").c_str(), &iconIndex, iconLabels.data(), (int)iconLabels.size()))
            ev.iconTexture = (iconIndex == 0) ? std::string() : iconFiles[iconIndex - 1];

        ImGui::SameLine();
        if (ImGui::SmallButton(TrId("WE_ICON_REFRESH", "###icon_rescan").c_str()))
            ScanEventIconFiles();

        //_ Free-text chat/map code for the copy-to-clipboard button; not a merge key, so no Save-button buffering like the name field.
        {
            char chatCodeBuf[128];
            strncpy(chatCodeBuf, ev.chatCode.c_str(), sizeof(chatCodeBuf) - 1);
            chatCodeBuf[sizeof(chatCodeBuf) - 1] = '\0';

            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputText(TrId("WE_TEXT_TO_COPY_LABEL", "##chat_code").c_str(), chatCodeBuf, sizeof(chatCodeBuf)))
                ev.chatCode = chatCodeBuf;
        }

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicGroupRow   (pairs with: DrawBasicEventRow)
//--------------------------------------------------------------------------------
// Draws one g_CyclicGroups[i] row in full (header, name, location, period,
// colors, idle-color override, and the nested per-slot list) - extracted the same
// way as DrawBasicEventRow, so both the "uncategorized" pass and each category's
// "members" pass can call the identical drawing code.
//
// PushID/PopID for this row are the CALLER's responsibility, same as
// DrawBasicEventRow.
//
// Sets pendingRemoveGroupIndex = i if this row's remove button was clicked this
// frame; does not modify g_CyclicGroups directly.
//--------------------------------------------------------------------------------
void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex)
{
    static std::map<int, std::string> editingNames;

    CyclicGroup& grp = g_CyclicGroups[i];

    //_ Bulk convenience over per-slot subscriptions, no storage of its own; checked only if every slot is subscribed, mixed reads unchecked.
    bool allSlotsSubscribed = !grp.slots.empty() &&
        std::all_of(grp.slots.begin(), grp.slots.end(), [&](const CyclicGroup::Slot& slot)
        {
            return IsCyclicSlotSubscribed(CyclicSubscriptionKey{ grp.id, slot.id });
        });
    if (DrawSubscribeCheckbox("##subscribe_group", allSlotsSubscribed))
    {
        for (const auto& slot : grp.slots)
        {
            CyclicSubscriptionKey key{ grp.id, slot.id };
            //_ allSlotsSubscribed already holds the post-click state: unticking drops every slot to 0, ticking only raises 0 -> 1.
            if (!allSlotsSubscribed)
            {
                SetCyclicSlotNotifyLevel(key, 0);
            }
            else if (GetCyclicSlotNotifyLevel(key) == 0)
            {
                SetCyclicSlotNotifyLevel(key, 1);
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SUBSCRIBE_CYCLE"));
    ImGui::SameLine();

    //_ Show/hide the ENTIRE ring (track + every slot); see CyclicGroup::shown in events.h.
    DrawSubscribeCheckbox("##show_group_on_map", grp.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_RING"));
    ImGui::SameLine();

    std::string oldGroupName = DisplayName(grp);
    const CyclicGroup* defaultGrp = GetDefaultCyclicGroup(grp.id);

    bool autoFocus = (s_pendingCyclicGroupEdit == i);
    if (autoFocus)
    {
        editingNames[i] = ""; //. freshly created - starts empty, forces the inline editor open
        s_pendingCyclicGroupEdit = -1;
    }
    bool isNew = (s_newCyclicGroupIndex == i);

    NameRowResult nameResult = DrawNameAndContextMenu("##group_node", i, i, DisplayName(grp), editingNames, pendingRemoveGroupIndex, kCyclicGroupDragType, grp.id,
        grp.apiMapChestId.empty() ? nullptr : "(auto)",
        nullptr, -1, nullptr,
        [&grp, defaultGrp]() { if (defaultGrp) grp = *defaultGrp; }, //. customName cleared for free - defaultGrp's own customName is always ""
        defaultGrp != nullptr, autoFocus, isNew);
    bool open = nameResult.open;
    if (nameResult.newName != oldGroupName)
        grp.customName = nameResult.newName;
    //_ Resolved (saved or cancelled) - frees the "+" button back up.
    if (isNew && (nameResult.cancelled || nameResult.newName != oldGroupName))
        s_newCyclicGroupIndex = -1;

    if (IsDuplicateGroupName(g_CyclicGroups, i))
        DrawDuplicateWarning();

    if (open)
    {
        //_ Compact row: Location, Period, Color, Idle override share one line; swatches use NoInputs (small square, full picker on click).
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputFloat2(Tr("WE_LOCATION_LABEL"), &grp.continentX, "%.0f");

        ImGui::SameLine();
        {
            char idSuffix[16];
            snprintf(idSuffix, sizeof(idSuffix), "cg%d", i);
            DrawDragButton(EditTarget::CyclicGroup, i, idSuffix);
        }

        ImGui::SetNextItemWidth(50.0f);
        DrawPeriodHoursDragInt(&grp.period);

        //_ colors.base is a plain ImVec4, so ColorEdit4 binds to it directly; no read/convert/write-back round trip needed.
        ImGui::ColorEdit4(Tr("WE_COLOR_LABEL"), &grp.colors.base.x, ImGuiColorEditFlags_AlphaBar |
                                                                         ImGuiColorEditFlags_NoInputs |
                                                                         ImGuiColorEditFlags_PickerHueWheel);

        //_ Optional override: unchecked uses colors.ter() (see CyclicGroup::IdleColor()); checked stores an explicit ImU32.
        ImGui::SameLine();
        bool hasCustomIdle = grp.idleColor.has_value();
        if (ImGui::Checkbox("##customcolorcyclicgroup", &hasCustomIdle))
        {
            if (hasCustomIdle)
                grp.idleColor = grp.colors.ter(); //. seed with current color
            else
                grp.idleColor.reset();
        }

        ImGui::SameLine();
        DisabledBlock(!hasCustomIdle)
        {
            ImU32 idleU32 = grp.idleColor.has_value() ? *grp.idleColor : grp.colors.ter();
            ImVec4 idleColorVec = ColorFloat4(idleU32);
            if (ImGui::ColorEdit4(TrId("WE_CUSTOM_COLOR_LABEL", "##group").c_str(), &idleColorVec.x, ImGuiColorEditFlags_AlphaBar |
                                                                                            ImGuiColorEditFlags_NoInputs |
                                                                                            ImGuiColorEditFlags_PickerHueWheel) && hasCustomIdle)
                grp.idleColor = ColorU32(idleColorVec);
        }

        //_ Slots are the individual events within this cycle; same deferred add/remove pattern, nested one PushID level deeper.
        ImGui::Spacing();
        ImGui::TextUnformatted(Tr("WE_GROUP_EVENTS_LABEL"));
        ImGui::SameLine();

        //_ Function-static, shared across every group; keyed by (group i, slot s) so slot 0 in different groups can't collide.
        static std::map<int, std::string> editingSlotNames;

        //_ One-shot; set on push, consumed next draw - same pattern as RequestBasicEventNameEdit, but local since add and draw both happen in this one function.
        static int s_pendingSlotEditKey = -1;

        //_ Persists (unlike s_pendingSlotEditKey) until that slot is saved/cancelled; shared across every group, same as editingSlotNames above.
        static int s_newSlotEditKey = -1;

        bool pendingAddSlot = false;
        DisabledBlock(s_newSlotEditKey >= 0)
        {
            pendingAddSlot = ImGui::SmallButton("+##add_slot");
        }
        if (s_newSlotEditKey >= 0 && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));

        int pendingRemoveSlotIndex = -1;

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            CyclicGroup::Slot& slot = grp.slots[s];
            ImGui::PushID(s);

            //_ Per SLOT, not per group; the group checkbox above is a bulk convenience over these same per-slot subscriptions.
            CyclicSubscriptionKey subKey{ grp.id, slot.id };
            int notifyLevel = GetCyclicSlotNotifyLevel(subKey);
            int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
            if (newNotifyLevel != notifyLevel)
                SetCyclicSlotNotifyLevel(subKey, newNotifyLevel);
            ImGui::SameLine();

            //_ Show/hide just THIS occurrence; the rest of the ring still draws (see CyclicGroup::Slot::shown in events.h).
            DrawSubscribeCheckbox("##show_slot_on_map", slot.shown);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_OCCURRENCE"));
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            const CyclicGroup::Slot* defaultSlot = GetDefaultCyclicSlot(grp.id, slot.id);
            std::string oldSlotName = DisplayName(slot, grp.id);

            bool slotAutoFocus = (s_pendingSlotEditKey == slotEditKey);
            if (slotAutoFocus)
            {
                editingSlotNames[slotEditKey] = ""; //. freshly created - starts empty, forces the inline editor open
                s_pendingSlotEditKey = -1;
            }
            bool slotIsNew = (s_newSlotEditKey == slotEditKey);

            //_ Slot rows aren't draggable (dragType left null) - a slot moves with its group, not independently between categories.
            NameRowResult slotNameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, oldSlotName, editingSlotNames, pendingRemoveSlotIndex,
                nullptr, std::string(), nullptr, [subKey]() { ToggleCyclicSlotDoneToday(subKey); },
                notifyLevel, [subKey](int lvl) { SetCyclicSlotNotifyLevel(subKey, lvl); },
                [&slot, defaultSlot]() { if (defaultSlot) slot = *defaultSlot; }, //. customName cleared for free - defaultSlot's own customName is always ""
                defaultSlot != nullptr, slotAutoFocus, slotIsNew);
            bool slotOpen = slotNameResult.open;
            //_ Slots aren't categorized and subscriptions key on (group id, slot id), not name, so no rename fixups are needed.
            if (slotNameResult.newName != oldSlotName)
                slot.customName = slotNameResult.newName;
            //_ Resolved (saved or cancelled) - frees the "+" button back up.
            if (slotIsNew && (slotNameResult.cancelled || slotNameResult.newName != oldSlotName))
                s_newSlotEditKey = -1;

            if (IsDuplicateSlotKey(grp.slots, s, grp.id))
                DrawDuplicateWarning();

            if (slotOpen)
            {
                ImGui::SetNextItemWidth(50.0f);
                int durationMinutes = slot.duration / 60;
                if (ImGui::DragInt(Tr("WE_DURATION_MIN_LABEL"), &durationMinutes, 0, 0, 0, "%dmin"))
                {
                    if (durationMinutes < 1) durationMinutes = 1;
                    slot.duration = durationMinutes * 60;
                }

                ImGui::SameLine();
                ImGui::Checkbox(Tr("WE_VARYING_CHECKBOX"), &slot.isVarying);

                if (!slot.isVarying)
                {
                    ImGui::SetNextItemWidth(50.0f);
                    int offsetMinutes = slot.offset / 60;
                    if (ImGui::DragInt(Tr("WE_OFFSET_LABEL"), &offsetMinutes, 0, 0, 0, "%dmin"))
                    {
                        if (offsetMinutes < 0) offsetMinutes = 0;
                        slot.offset = offsetMinutes * 60;
                    }

                    //_ Repeat must evenly divide the period; snaps down to the nearest divisor of the CURRENT period, re-checked every frame.
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    int repeatInput = slot.repeat;
                    if (ImGui::InputInt(Tr("WE_REPETITION_LABEL"), &repeatInput, 0, 0))
                    {
                        if (repeatInput < 1) repeatInput = 1;
                        if (repeatInput > grp.period) repeatInput = grp.period;
                        while (repeatInput > 1 && grp.period % repeatInput != 0)
                            repeatInput--;
                        slot.repeat = repeatInput;
                    }
                    else if (grp.period % slot.repeat != 0)
                    {
                        //_ Period changed elsewhere (e.g. the dropdown above)
                        // and no longer divides evenly - snap down the same way.
                        int fixed = slot.repeat;
                        while (fixed > 1 && grp.period % fixed != 0)
                            fixed--;
                        slot.repeat = fixed;
                    }
                    Tooltip(Tr("WE_TIP_REPETITION"));
                }
                else
                {
                    //_ Sorted minute-into-period times, not HH:MM (period isn't always 24h); offset/repeat are unused while isVarying is set (see events.h).
                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("WE_TIMES_MIN_INTO_PERIOD_LABEL"));
                    ImGui::SameLine();
                    bool pendingAddTime = ImGui::SmallButton("+##add_slot_time");

                    int pendingRemoveTimeIndex = -1;
                    int periodMinutes = grp.period / 60;

                    for (int t = 0; t < (int)slot.varyingTimes.size(); t++)
                    {
                        ImGui::PushID(t);

                        int minutes = slot.varyingTimes[t] / 60;
                        bool changed = false;
                        ImGui::SetNextItemWidth(50.0f);
                        if (ImGui::InputInt("##slotVaryingTime", &minutes, 0, 0))
                        {
                            minutes = std::clamp(minutes, 0, periodMinutes > 0 ? periodMinutes - 1 : 0);
                            changed = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextUnformatted(Tr("WE_MINUTES_UNIT_LABEL"));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("-##remove_slot_time"))
                            pendingRemoveTimeIndex = t;

                        if (changed)
                            slot.varyingTimes[t] = minutes * 60;

                        ImGui::PopID();
                    }

                    if (pendingRemoveTimeIndex >= 0)
                        slot.varyingTimes.erase(slot.varyingTimes.begin() + pendingRemoveTimeIndex);

                    if (pendingAddTime)
                        slot.varyingTimes.push_back(0);

                    //_ Re-sorted every frame; GetSubscriptionActiveState's cyclic-varying branch (subscriptions_cache.cpp) requires ascending order.
                    std::sort(slot.varyingTimes.begin(), slot.varyingTimes.end());
                }

                ImGui::SetNextItemWidth(100.0f);
                const char* kTierLabels[] = { Tr("WE_TIER_PRIMARY"), Tr("WE_TIER_SECONDARY"), Tr("WE_TIER_TERTIARY") };
                int tierIndex = (int)slot.tier;
                if (ImGui::Combo(TrId("WE_TIER_LABEL", "##slot_tier").c_str(), &tierIndex, kTierLabels, 3))
                    slot.tier = (ColorTier)tierIndex;

                //_ Same checkbox-gates-swatch pattern as Custom Idle above; seeded from the slot's current resolved color.
                ImGui::SameLine();
                bool hasCustomColor = slot.customColor.has_value();
                if (ImGui::Checkbox("##customcolorcyclicslot", &hasCustomColor))
                {
                    if (hasCustomColor)
                        slot.customColor = grp.SlotColor(slot);
                    else
                        slot.customColor.reset();
                }

                ImGui::SameLine();
                DisabledBlock(!hasCustomColor)
                {
                    ImU32 slotU32 = slot.customColor.has_value() ? *slot.customColor : grp.SlotColor(slot);
                    ImVec4 slotColorVec = ColorFloat4(slotU32);
                    if (ImGui::ColorEdit4(TrId("WE_CUSTOM_COLOR_LABEL", "##slot").c_str(), &slotColorVec.x, ImGuiColorEditFlags_AlphaBar |
                                                                                                   ImGuiColorEditFlags_NoInputs |
                                                                                                   ImGuiColorEditFlags_PickerHueWheel) && hasCustomColor)
                        slot.customColor = ColorU32(slotColorVec);
                }

                //_ Same as WorldEvent::chatCode; not a merge key, so it live-edits directly with no Save-button buffering.
                {
                    char chatCodeBuf[128];
                    strncpy(chatCodeBuf, slot.chatCode.c_str(), sizeof(chatCodeBuf) - 1);
                    chatCodeBuf[sizeof(chatCodeBuf) - 1] = '\0';

                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText(TrId("WE_TEXT_TO_COPY_LABEL", "##slot_chat_code").c_str(), chatCodeBuf, sizeof(chatCodeBuf)))
                        slot.chatCode = chatCodeBuf;
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (pendingRemoveSlotIndex >= 0)
            grp.slots.erase(grp.slots.begin() + pendingRemoveSlotIndex);

        if (pendingAddSlot)
        {
            s_pendingSlotEditKey = i * 100000 + (int)grp.slots.size(); //. index this slot will land at, below
            s_newSlotEditKey     = s_pendingSlotEditKey;

            //_ Slot ids only need to be unique within this group (events_storage.cpp).
            std::unordered_set<std::string> usedSlotIds;
            for (const auto& s : grp.slots) usedSlotIds.insert(s.id);

            CyclicGroup::Slot newSlot{};
            //_ id seed is a fixed ASCII word, not the (empty) display default - SlugifyName strips non-ASCII to nothing (events_storage.cpp).
            newSlot.id       = UniqueId(SlugifyName("slot"), usedSlotIds);
            newSlot.customName = ""; //. starts unnamed - forces the inline editor open on next draw (see s_pendingSlotEditKey above)
            newSlot.offset   = 0;
            newSlot.duration = 600; //. 10 min, a reasonable default
            newSlot.tier     = ColorTier::Primary;
            newSlot.repeat   = 1;
            grp.slots.push_back(newSlot);
        }

        ImGui::TreePop();
    }
}