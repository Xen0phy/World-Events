//################################################################################
// options_events_rows.cpp   (see: options_events_rows.h)
//--------------------------------------------------------------------------------
// kMinPeriodHours/kMaxPeriodHours
//                            whole-hour bounds of a period
// PeriodSecondsToHours/DrawPeriodHoursDragInt
//                            period widget shared by both row kinds
// IsDuplicateName            same visible name as another entry
// DrawDuplicateWarning       the "[duplicate]" tag beside such a row
// DragPayload                fixed-size copy of a dragged item's id
// MakeDragSource             drag source for a row
// DrawNotifyGlyph            minus, plus, bell or speaker for one notify level
// DrawNotifyLevelIcon/DrawNotifyLevelButtons
//                            the two notify-level controls
// DrawDragButton             map-drag edit mode toggle
// DrawFixToScreenRow         pin a marker or ring to a screen position
// DrawOptionalColor          checkbox that gates a color swatch for an optional override
// s_basicEventEdit/s_cyclicGroupEdit
//                            name-edit state of the two row kinds
// kLinkFlashId               highlight id shared by every row
// BeginLinkedRow             start of a row a deep link may name
//--------------------------------------------------------------------------------

#include "options_events_rows.h"

#include "color_utils.h"
#include "events.h"
#include "events_storage.h" //. GetDefaultEvent/GetDefaultCyclicGroup/GetDefaultCyclicSlot/DisplayName/NewUniqueId
#include "events_tracking.h"
#include "imgui.h"
#include "localization.h"
#include "maprender.h" //. EditTarget/g_EditMode, ScreenFractionToPixels/PixelsToScreenFraction
#include "options_widgets.h" //. Tooltip, DisabledBlock, DrawSubscribeCheckbox, DrawBellIcon/DrawSpeakerIcon, DrawFileCombo, kAlphaSwatchFlags, kWarningColor
#include "options_window.h" //. OptionsHighlight_Set/IsActive, for BeginLinkedRow
#include "subscriptions.h"

#include <algorithm>
#include <cstring>
#include <optional>

//_ Period is whole hours only, 1-12h; see PeriodSecondsToHours for why.
static constexpr int kMinPeriodHours = 1;
static constexpr int kMaxPeriodHours = 12;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PeriodSecondsToHours / DrawPeriodHoursDragInt
//--------------------------------------------------------------------------------
// Whole hours only, 1-12h: no GW2 event/chain runs on anything but a whole-hour
// cycle. DrawPeriodHoursDragInt is the period widget of both the Basic Event row
// and the Cyclic Group row; a DragInt, with no fixed label array, leaves room to
// raise the cap later without code changes. PeriodSecondsToHours is the
// seconds->hours conversion it is built on: it clamps to [kMinPeriodHours,
// kMaxPeriodHours], snapping any out-of-range or non-whole-hour value (e.g. from
// a hand-edited JSON file) to the nearest valid hour.
//--------------------------------------------------------------------------------
static int PeriodSecondsToHours(int periodSeconds)
{
    return std::clamp(periodSeconds / 3600, kMinPeriodHours, kMaxPeriodHours);
}

static void DrawPeriodHoursDragInt(int* periodSeconds)
{
    int hours = PeriodSecondsToHours(*periodSeconds);
    if (ImGui::DragInt(Tr("WE_PERIOD_LABEL"), &hours, 0.1f, kMinPeriodHours, kMaxPeriodHours, "%dh"))
    {
        //_ DragInt's min/max only clamp the drag gesture; a typed (ctrl+click) value can still land outside range, so clamp explicitly.
        hours = std::clamp(hours, kMinPeriodHours, kMaxPeriodHours);
        *periodSeconds = hours * 3600;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsDuplicateName / DrawDuplicateWarning
//--------------------------------------------------------------------------------
// Display-only warning: flags two entries sharing a visible name, so the player
// can tell them apart in the UI. Not a merge-key check: GroupKey/EventKey/
// SlotKey (events_storage.cpp) key on id, so a duplicate name here does not mean
// a duplicate identity. IsDuplicateName compares DisplayName (events_storage.h),
// the resolved/localized text, not the raw customName: two events with different
// customName can still collide once one falls through to a compiled default's
// translation. selfIndex excludes the entry being checked; nameArgs follow the
// item into DisplayName (a slot name is unique WITHIN its group only, so the slot
// call passes the group id). DrawDuplicateWarning draws the "[duplicate]" tag
// next to a row whose check came back true.
//--------------------------------------------------------------------------------
template <typename Item, typename... NameArgs>
static bool IsDuplicateName(const std::vector<Item>& items, int selfIndex, const NameArgs&... nameArgs)
{
    std::string name = DisplayName(items[selfIndex], nameArgs...);
    if (name.empty()) return false;
    for (int i = 0; i < (int)items.size(); i++)
        if (i != selfIndex && DisplayName(items[i], nameArgs...) == name)
            return true;
    return false;
}

static void DrawDuplicateWarning()
{
    ImGui::SameLine();
    ImGui::TextColored(kWarningColor, "%s", Tr("WE_DUPLICATE_TAG"));
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
// MakeDragSource   (pairs with: MakeDropTarget)
//--------------------------------------------------------------------------------
// Call right after the widget being dragged (e.g. a row's TreeNode). itemId is
// the payload moved into Category::members on drop; displayName is only the text
// shown under the cursor while dragging.
//--------------------------------------------------------------------------------
static void MakeDragSource(const char* dragType, const std::string& itemId, const std::string& displayName)
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MakeDropTarget   (see: options_events_rows.h)
//--------------------------------------------------------------------------------
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
// DrawNotifyGlyph
//--------------------------------------------------------------------------------
// Level 0..3 of the notify ladder (unsubscribed / silent / toast / toast+sound)
// as minus / plus / bell / speaker, centered in the sq-sided box at rmin. scale
// is the bell and speaker size as a fraction of sq; the minus and plus strokes
// run the box's width less a small pad.
//--------------------------------------------------------------------------------
static void DrawNotifyGlyph(ImDrawList* dl, ImVec2 rmin, float sq, int level, float scale)
{
    ImVec2 rmax(rmin.x + sq, rmin.y + sq);
    ImVec2 center((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
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
            DrawBellIcon(dl, center, sq * scale, col);
            break;
        default: //. level 3 - toast + sound - speaker
            DrawSpeakerIcon(dl, center, sq * scale, col);
            break;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelIcon / DrawNotifyLevelButtons
//--------------------------------------------------------------------------------
// Two views of the 0..3 notify ladder, drawn by DrawNotifyGlyph. Both are manual
// hit-tests over ImDrawList, not real widgets, and return the level to apply this
// frame (unchanged unless clicked). DrawNotifyLevelIcon is one frame-height icon
// at the front of each event and slot row: it shows the current level and a left-
// click advances one level, wrapping 3 -> 0. DrawNotifyLevelButtons is four hit-
// boxes side by side in the expanded body: each jumps straight to its level and
// the current one is framed. The right-click menu jumps too, through
// DrawNameAndContextMenu's notifyLevel/setNotifyLevel.
//--------------------------------------------------------------------------------
static int DrawNotifyLevelIcon(const char* idSuffix, int level)
{
    ImGui::PushID(idSuffix);

    //_ Same reasoning as DrawSubscribeCheckbox: zero FramePadding so this icon matches the tree arrow's height.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    float sq = ImGui::GetFrameHeight();
    ImVec2 rmin = ImGui::GetCursorScreenPos();
    ImVec2 rmax(rmin.x + sq, rmin.y + sq);

    bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rmin, rmax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered)
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    DrawNotifyGlyph(dl, rmin, sq, level, 0.96f);

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

static int DrawNotifyLevelButtons(const char* idSuffix, int level)
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

        DrawNotifyGlyph(dl, rmin, sq, lvl, 0.9f);

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
// Small button next to the Location field that arms/disarms map-drag edit mode
// for one Basic Event or Cyclic Group (see EditModeState in maprender.h). Reads
// "Drag" when this row isn't the one being edited and "Stop" when it is; clicking
// toggles. The hover tooltip says WHERE to drag (the marker on the map, not this
// button), which "Drag"/"Stop" alone doesn't.
//--------------------------------------------------------------------------------
static void DrawDragButton(EditTarget target, int index)
{
    bool isBeingEdited = (g_EditMode.target == target && g_EditMode.index == index);

    if (ImGui::SmallButton(isBeingEdited ? "Stop##drag_btn" : "Drag##drag_btn"))
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
// DrawFixToScreenRow
//--------------------------------------------------------------------------------
// Pins a marker/ring to a fixed spot on the player's own screen
// (fixedToScreen/screenX/screenY, events.h) in place of a map position. Drawn
// right after the continent-space Location row, same visual weight (100px-wide
// InputFloat2). screenX/Y are stored as a normalized [0,1] fraction of the game
// window so the pinned spot survives a resolution or window-size change (see
// ScreenFractionToPixels/PixelsToScreenFraction, maprender.h), but shown and
// edited as PIXEL coordinates, which is what a player lining something up on
// their own screen wants; every edit converts back to the fraction. The ids are
// fixed; the caller's PushID keeps the rows apart.
//--------------------------------------------------------------------------------
static void DrawFixToScreenRow(bool* fixedToScreen, float* screenX, float* screenY)
{
    ImGui::Checkbox(TrId("WE_FIX_TO_SCREEN_LABEL", "##fix_to_screen").c_str(), fixedToScreen);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_FIX_TO_SCREEN"));

    if (*fixedToScreen)
    {
        ImVec2 px = ScreenFractionToPixels(*screenX, *screenY);
        float pixelPos[2] = { px.x, px.y };

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputFloat2(TrId("WE_SCREEN_POS_LABEL", "##fix_to_screen").c_str(), pixelPos, "%.0f"))
        {
            ImVec2 frac = PixelsToScreenFraction({ pixelPos[0], pixelPos[1] });
            *screenX = frac.x;
            *screenY = frac.y;
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionalColor
//--------------------------------------------------------------------------------
// A checkbox and a color swatch for a std::optional<ImU32> override, both on the
// current line. Ticking seeds the override with fallback(); unticking clears it.
// While unticked the swatch is dimmed, shows fallback() and writes nothing back.
// fallback is a callable, called after the checkbox has changed the override,
// because CyclicGroup::SlotColor returns the override once one is set. checkboxId
// and colorLabel are full ImGui labels.
//--------------------------------------------------------------------------------
template <typename Fallback>
static void DrawOptionalColor(const char* checkboxId, const char* colorLabel, std::optional<ImU32>& color, Fallback fallback)
{
    ImGui::SameLine();
    bool hasColor = color.has_value();
    if (ImGui::Checkbox(checkboxId, &hasColor))
    {
        if (hasColor)
            color = fallback();
        else
            color.reset();
    }

    ImGui::SameLine();
    DisabledBlock(!hasColor)
    {
        ImVec4 colorVec = ColorFloat4(color.has_value() ? *color : fallback());
        if (ImGui::ColorEdit4(colorLabel, &colorVec.x, kAlphaSwatchFlags) && hasColor)
            color = ColorU32(colorVec);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNameAndContextMenu
//--------------------------------------------------------------------------------
// toggleDone, notifyLevel/setNotifyLevel, and resetToDefault add optional right-
// click entries, left null/-1 where unused (categories pass none). resetToDefault
// adds Reset, greyed via resetAvailable for entries with no compiled-in default
// (see GetDefault* in events_storage.h). edit.names keys by editKey, not
// removeIndex - slots share one state across groups (DrawCyclicGroupRow).
// Delete/Cancel erase the edit.names entry and set pendingRemoveIndex =
// removeIndex. A saved row's Delete swaps the menu for an inline confirm/cancel
// choice (confirmingDelete below).
//--------------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(
    const char*              treeNodeId,
    int                      editKey,
    int                      removeIndex,
    const std::string&       currentName,
    NameEditState&           edit,
    int&                     pendingRemoveIndex,
    const char*              dragType,
    const std::string&       dragId,
    const char*              autoTag,
    std::function<void()>    toggleDone,
    int                      notifyLevel,
    std::function<void(int)> setNotifyLevel,
    std::function<void()>    resetToDefault,
    bool                     resetAvailable)
{
    const bool autoFocus = (edit.pendingFocus == editKey);
    if (autoFocus)
    {
        edit.names[editKey] = ""; //. freshly created - starts empty, forces the inline editor open
        edit.pendingFocus = -1;
    }
    const bool isNew = (edit.newKey == editKey);

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
        //_ Confirm-step flag lives in ImGui's state storage (survives while the popup stays open); reset whenever it reappears so a stale confirm doesn't carry over
        ImGuiID confirmDeleteId = ImGui::GetID("##confirm_delete_inline");
        if (ImGui::IsWindowAppearing())
            ImGui::GetStateStorage()->SetBool(confirmDeleteId, false);
        bool confirmingDelete = ImGui::GetStateStorage()->GetBool(confirmDeleteId, false);

        if (confirmingDelete)
        {
            //_ Both Selectables default-close the popup on click same as MenuItem (imgui_widgets.cpp); Confirm commits the removal below first, Cancel just lets the close happen
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 16.0f);
            ImGui::TextWrapped(Tr("WE_DELETE_CONFIRM_BODY_FMT"), label.c_str());
            ImGui::TextWrapped("%s", Tr("WE_DELETE_CONFIRM_HINT"));
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            if (ImGui::Selectable(Tr("WE_DELETE_CONFIRM_BUTTON")))
            {
                pendingRemoveIndex = removeIndex;
                edit.names.erase(editKey); //. matches the isNew branch below - Delete always clears any in-progress edit
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Selectable(Tr("WE_DELETE_CANCEL_BUTTON")))
                ImGui::CloseCurrentPopup();
        }
        else
        {
            if (toggleDone)
            {
                if (ImGui::MenuItem(Tr("WE_SUBS_MARK_DONE_TODAY")))
                    toggleDone();
                ImGui::Separator();
            }
            if (setNotifyLevel && notifyLevel >= 0)
            {
                //_ Jump menu past the forward-only cycle; current stage shows a checkmark.
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
                edit.names[editKey] = currentName; //. seeded when edit starts
            ImGui::Separator();
            if (resetToDefault)
            {
                if (ImGui::MenuItem(Tr("WE_ROW_RESET"), nullptr, false, resetAvailable))
                    resetToDefault();
                ImGui::Separator();
            }
            if (isNew)
            {
                if (ImGui::MenuItem(Tr("WE_ROW_DELETE")))
                {
                    pendingRemoveIndex = removeIndex;
                    edit.names.erase(editKey);
                }
            }
            else
            {
                //_ DontClosePopups here - a plain Selectable/MenuItem click auto-closes the popup and would lose confirmDeleteId's new "true" before confirmingDelete can show it next frame
                if (ImGui::Selectable(Tr("WE_ROW_DELETE"), false, ImGuiSelectableFlags_DontClosePopups))
                    ImGui::GetStateStorage()->SetBool(confirmDeleteId, true);
            }
        }
        ImGui::EndPopup();
    }

    auto it = edit.names.find(editKey);
    if (it == edit.names.end())
    {
        //_ missing name entry: either branch above erased it (Delete of a new entry, or a confirmed delete on any row); a deleted new entry is resolved
        if (isNew)
            edit.newKey = -1;
        return { open, currentName };
    }
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
            edit.names.erase(it);
            if (isNew && saved != currentName)
                edit.newKey = -1; //. resolved: frees the "+" button
            return { open, saved };
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
            edit.names.erase(it);
            edit.newKey = -1; //. resolved: frees the "+" button
            pendingRemoveIndex = removeIndex;
            return { open, currentName };
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Tr("WE_TIP_CANCEL_NEW"));
    }

    return { open, currentName }; //. unchanged until Save/Cancel is clicked
}

//_ Name-edit state of the Basic event rows; the Request call sets its pendingFocus and newKey.
static NameEditState s_basicEventEdit;

//_ Name-edit state of the Cyclic group rows.
static NameEditState s_cyclicGroupEdit;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestBasicEventNameEdit / RequestCyclicGroupNameEdit   (see: options_events_rows.h)
//--------------------------------------------------------------------------------
void RequestBasicEventNameEdit(int index)
{
    s_basicEventEdit.pendingFocus = index;
    s_basicEventEdit.newKey       = index;
}

void RequestCyclicGroupNameEdit(int index)
{
    s_cyclicGroupEdit.pendingFocus = index;
    s_cyclicGroupEdit.newKey       = index;
}

bool IsBasicEventCreationPending()  { return s_basicEventEdit.newKey  >= 0; }
bool IsCyclicGroupCreationPending() { return s_cyclicGroupEdit.newKey >= 0; }

//_ One id for every row: RowMode::linked decides which row matches it.
static constexpr const char* kLinkFlashId = "events_linked_row";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BeginLinkedRow
//--------------------------------------------------------------------------------
// Call before a row's first item; linked says whether the deep link names this
// row and linkPending whether it has not landed yet. Returns true on the frame
// the link lands on the row: the caller then scrolls to it and opens its node,
// and the flash starts. While the flash runs (OptionsHighlight_Set,
// options_window.h), the row's first line gets a Header tint. It is drawn ahead
// of the row's items, so they sit on top of it.
//--------------------------------------------------------------------------------
static bool BeginLinkedRow(bool linked, bool linkPending)
{
    if (!linked)
        return false;

    const bool landing = linkPending;
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
// DrawBasicEventRow   (pairs with: DrawCyclicGroupRow)
//--------------------------------------------------------------------------------
// The header is the same in both modes: notify icon, show-on-map checkbox, then
// the DrawNameAndContextMenu row ("(auto)" tag, drag source, right-click menu,
// duplicate warning). The expanded body starts with the four-way notify buttons
// and Done for today, re-reading the level since the icon may have changed it
// this frame; the editing fields follow when mode.deep is set. On the frame a
// pending deep link lands (BeginLinkedRow) the row scrolls to the middle of the
// pane after its first item and forces its node open.
//--------------------------------------------------------------------------------
void DrawBasicEventRow(int i, const RowMode& mode, int& pendingRemoveIndex)
{
    WorldEvent& ev = g_Events[i];
    const bool landing = BeginLinkedRow(mode.linked, mode.linkPending);

    //_ Drawn before the name/tree-arrow, in the slot DrawSubscribeCheckbox used to occupy; see subscriptions.h for what each level touches.
    int notifyLevel = GetBasicEventNotifyLevel(ev.id);
    int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.id, newNotifyLevel);
    if (landing)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item
    ImGui::SameLine();

    //_ Map-only show/hide; the Subscriptions bar/window are unaffected (that's the checkbox above). ev.shown defaults to true.
    DrawSubscribeCheckbox("##show_on_map", ev.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_ON_MAP"));
    ImGui::SameLine();

    std::string oldName = DisplayName(ev);
    const WorldEvent* defaultEv = GetDefaultEvent(ev.id);

    //_ Always, not Once: a repeat link to the same row must reopen it.
    if (landing)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    NameRowResult nameResult = DrawNameAndContextMenu("##event_node", i, i, DisplayName(ev), s_basicEventEdit, pendingRemoveIndex, kBasicEventDragType, ev.id,
        ev.apiWorldBossId.empty() ? nullptr : "(auto)",
        [&ev]() { ToggleBasicEventDoneToday(ev.id); },
        notifyLevel, [&ev](int lvl) { SetBasicEventNotifyLevel(ev.id, lvl); },
        [&ev, defaultEv]() { if (defaultEv) ev = *defaultEv; }, //. customName cleared for free - defaultEv's own customName is always ""
        defaultEv != nullptr);
    bool open = nameResult.open;
    if (nameResult.newName != oldName)
    {
        ev.customName = nameResult.newName;
    }

    if (IsDuplicateName(g_Events, i))
        DrawDuplicateWarning();

    if (open)
    {
        int level = GetBasicEventNotifyLevel(ev.id);
        int newLevel = DrawNotifyLevelButtons("##notify_buttons", level);
        if (newLevel != level)
            SetBasicEventNotifyLevel(ev.id, newLevel);

        bool doneToday = IsBasicEventMarkedDoneToday(ev.id);
        if (ImGui::Checkbox(Tr("WE_OPTWIN_QUICK_DONE_TODAY"), &doneToday))
            ToggleBasicEventDoneToday(ev.id);

        if (mode.deep)
        {
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputFloat2(Tr("WE_LOCATION_LABEL"), &ev.continentX, "%.0f");

            ImGui::SameLine();
            DrawDragButton(EditTarget::BasicEvent, i);

            DrawFixToScreenRow(&ev.fixedToScreen, &ev.screenX, &ev.screenY);

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

            //_ "Dot" (the empty name) keeps the plain circle; any other entry names a textures/ file tinted to the status color (see maprender.cpp).
            DrawFileCombo(TrId("WE_ICON_LABEL", "##event_icon").c_str(), "WE_ICON_DOT", GetEventIconFilenames(), ev.iconTexture);

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
        }

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicGroupRow   (pairs with: DrawBasicEventRow)
//--------------------------------------------------------------------------------
// The header is the show-ring checkbox and the name row; the expanded body starts
// with Subscribe all. With mode.deep, the location, period and color fields
// follow, then the "Group events" label with the add-slot button. The slot rows
// nest in both modes: the same header and Quick body as a Basic event, with their
// own editing fields under mode.deep. A link to the group scrolls after the ring
// checkbox; a link to a slot opens the group and lands on that slot's row, which
// flashes alone.
//--------------------------------------------------------------------------------
void DrawCyclicGroupRow(int i, const RowMode& mode, int& pendingRemoveGroupIndex)
{
    CyclicGroup& grp = g_CyclicGroups[i];

    //_ Set when the link names one of this group's slots; the group then opens but only that slot lands.
    const std::string* slotTargetId = (mode.linked && mode.linkedSlotId && !mode.linkedSlotId->empty()) ? mode.linkedSlotId : nullptr;
    const bool landing = BeginLinkedRow(mode.linked && !slotTargetId, mode.linkPending);

    //_ Show/hide the ENTIRE ring (track + every slot); see CyclicGroup::shown in events.h.
    DrawSubscribeCheckbox("##show_group_on_map", grp.shown);
    if (landing)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_RING"));
    ImGui::SameLine();

    std::string oldGroupName = DisplayName(grp);
    const CyclicGroup* defaultGrp = GetDefaultCyclicGroup(grp.id);

    //_ Always, not Once: a repeat link must reopen the row; a link to one of its slots opens the group too.
    if (mode.linked && mode.linkPending)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    NameRowResult nameResult = DrawNameAndContextMenu("##group_node", i, i, DisplayName(grp), s_cyclicGroupEdit, pendingRemoveGroupIndex, kCyclicGroupDragType, grp.id,
        grp.apiMapChestId.empty() ? nullptr : "(auto)",
        nullptr, -1, nullptr,
        [&grp, defaultGrp]() { if (defaultGrp) grp = *defaultGrp; }, //. customName cleared for free - defaultGrp's own customName is always ""
        defaultGrp != nullptr);
    bool open = nameResult.open;
    if (nameResult.newName != oldGroupName)
        grp.customName = nameResult.newName;

    if (IsDuplicateName(g_CyclicGroups, i))
        DrawDuplicateWarning();

    if (open)
    {
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
        ImGui::TextUnformatted(Tr("WE_OPTWIN_QUICK_SUBSCRIBE_ALL"));

        //_ Function-static, shared across every group; keyed by (group i, slot s) so slot 0 in different groups can't collide.
        static NameEditState slotEdit;

        bool pendingAddSlot = false;

        if (mode.deep)
        {
            //_ Compact row: Location, Period, Color, Idle override share one line; swatches use NoInputs (small square, full picker on click).
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputFloat2(Tr("WE_LOCATION_LABEL"), &grp.continentX, "%.0f");

            ImGui::SameLine();
            DrawDragButton(EditTarget::CyclicGroup, i);

            DrawFixToScreenRow(&grp.fixedToScreen, &grp.screenX, &grp.screenY);

            ImGui::SetNextItemWidth(50.0f);
            DrawPeriodHoursDragInt(&grp.period);

            //_ colors.base is a plain ImVec4, so ColorEdit4 binds to it directly; no read/convert/write-back round trip needed.
            ImGui::ColorEdit4(Tr("WE_COLOR_LABEL"), &grp.colors.base.x, kAlphaSwatchFlags);

            //_ Optional override: unchecked uses colors.ter() (see CyclicGroup::IdleColor()); checked stores an explicit ImU32.
            DrawOptionalColor("##customcolorcyclicgroup", TrId("WE_CUSTOM_COLOR_LABEL", "##group").c_str(), grp.idleColor,
                [&] { return grp.colors.ter(); });

            //_ Slots are the individual events within this cycle; same deferred add/remove pattern, nested one PushID level deeper.
            ImGui::Spacing();
            ImGui::TextUnformatted(Tr("WE_GROUP_EVENTS_LABEL"));
            ImGui::SameLine();

            DisabledBlock(slotEdit.newKey >= 0)
            {
                pendingAddSlot = ImGui::SmallButton("+##add_slot");
            }
            if (slotEdit.newKey >= 0 && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_FINISH_NAMING"));
        }

        int pendingRemoveSlotIndex = -1;

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            CyclicGroup::Slot& slot = grp.slots[s];
            ImGui::PushID(s);
            const bool slotLanding = BeginLinkedRow(slotTargetId && *slotTargetId == slot.id, mode.linkPending);

            //_ Per SLOT, not per group; the group checkbox above is a bulk convenience over these same per-slot subscriptions.
            CyclicSubscriptionKey subKey{ grp.id, slot.id };
            int notifyLevel = GetCyclicSlotNotifyLevel(subKey);
            int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
            if (newNotifyLevel != notifyLevel)
                SetCyclicSlotNotifyLevel(subKey, newNotifyLevel);
            if (slotLanding)
                ImGui::SetScrollHereY(0.5f); //. after the row's first item
            ImGui::SameLine();

            //_ Show/hide just THIS occurrence; the rest of the ring still draws (see CyclicGroup::Slot::shown in events.h).
            DrawSubscribeCheckbox("##show_slot_on_map", slot.shown);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("WE_TIP_SHOW_OCCURRENCE"));
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            const CyclicGroup::Slot* defaultSlot = GetDefaultCyclicSlot(grp.id, slot.id);
            std::string oldSlotName = DisplayName(slot, grp.id);

            if (slotLanding)
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);

            //_ Slot rows aren't draggable (dragType left null) - a slot moves with its group, not independently between categories.
            NameRowResult slotNameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, oldSlotName, slotEdit, pendingRemoveSlotIndex,
                nullptr, std::string(), nullptr, [subKey]() { ToggleCyclicSlotDoneToday(subKey); },
                notifyLevel, [subKey](int lvl) { SetCyclicSlotNotifyLevel(subKey, lvl); },
                [&slot, defaultSlot]() { if (defaultSlot) slot = *defaultSlot; }, //. customName cleared for free - defaultSlot's own customName is always ""
                defaultSlot != nullptr);
            bool slotOpen = slotNameResult.open;
            //_ Slots aren't categorized and subscriptions key on (group id, slot id), not name, so no rename fixups are needed.
            if (slotNameResult.newName != oldSlotName)
                slot.customName = slotNameResult.newName;

            if (IsDuplicateName(grp.slots, s, grp.id))
                DrawDuplicateWarning();

            if (slotOpen)
            {
                int level = GetCyclicSlotNotifyLevel(subKey);
                int newLevel = DrawNotifyLevelButtons("##notify_buttons", level);
                if (newLevel != level)
                    SetCyclicSlotNotifyLevel(subKey, newLevel);

                bool doneToday = IsCyclicSlotMarkedDoneToday(subKey);
                if (ImGui::Checkbox(Tr("WE_OPTWIN_QUICK_DONE_TODAY"), &doneToday))
                    ToggleCyclicSlotDoneToday(subKey);

                if (mode.deep)
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

                    //_ Same override as the group's custom idle color; seeded from the slot's resolved tier color.
                    DrawOptionalColor("##customcolorcyclicslot", TrId("WE_CUSTOM_COLOR_LABEL", "##slot").c_str(), slot.customColor,
                        [&] { return grp.SlotColor(slot); });

                    //_ Same as WorldEvent::chatCode; not a merge key, so it live-edits directly with no Save-button buffering.
                    {
                        char chatCodeBuf[128];
                        strncpy(chatCodeBuf, slot.chatCode.c_str(), sizeof(chatCodeBuf) - 1);
                        chatCodeBuf[sizeof(chatCodeBuf) - 1] = '\0';

                        ImGui::SetNextItemWidth(160.0f);
                        if (ImGui::InputText(TrId("WE_TEXT_TO_COPY_LABEL", "##slot_chat_code").c_str(), chatCodeBuf, sizeof(chatCodeBuf)))
                            slot.chatCode = chatCodeBuf;
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if (pendingRemoveSlotIndex >= 0)
            grp.slots.erase(grp.slots.begin() + pendingRemoveSlotIndex);

        if (pendingAddSlot)
        {
            slotEdit.pendingFocus = i * 100000 + (int)grp.slots.size(); //. index this slot will land at, below
            slotEdit.newKey       = slotEdit.pendingFocus;

            CyclicGroup::Slot newSlot{};
            //_ Slot ids only need to be unique within this group (events_storage.cpp).
            newSlot.id       = NewUniqueId("slot", grp.slots);
            newSlot.customName = ""; //. starts unnamed - forces the inline editor open on next draw (see slotEdit above)
            newSlot.offset   = 0;
            newSlot.duration = 600; //. 10 min, a reasonable default
            newSlot.tier     = ColorTier::Primary;
            newSlot.repeat   = 1;
            grp.slots.push_back(newSlot);
        }

        ImGui::TreePop();
    }
}
