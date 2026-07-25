//################################################################################
// addon_options_helpers.cpp
//--------------------------------------------------------------------------------
// Implementations for everything declared in addon_options_helpers.h -
// the scoped-disable helper, period widget, bulk icon picker, color
// conversion, duplicate-name checks, drag-and-drop plumbing, hand-drawn
// glyphs, the notify-level control, the shared name/context-menu row,
// search predicates, and the two full row drawers (Basic Event / Cyclic
// Group). See addon_options.cpp for the panel that assembles these into
// the actual Nexus options UI.
//--------------------------------------------------------------------------------

#include "addon_options_helpers.h"
#include "color_utils.h"
#include "events_tracking.h"
#include "imgui_internal.h" //. for internal-only ImGui APIs
#include "subscriptions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

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
// Whole hours only, 1-12h: no GW2 event/chain runs on anything but a
// whole-hour cycle, and a DragInt (rather than a Combo over a fixed
// label array) leaves room to raise the cap later without code changes.
//
// PeriodSecondsToHours clamps to [kMinPeriodHours, kMaxPeriodHours],
// snapping any out-of-range or non-whole-hour value (e.g. from a
// hand-edited JSON file) to the nearest valid hour.
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
    if (ImGui::DragInt("Period", &hours, 0.1f, kMinPeriodHours, kMaxPeriodHours, "%dh"))
    {
        //_ DragInt's min/max only clamp the drag gesture - a typed value
        // (ctrl+click) can still land outside range, so clamp explicitly.
        if (hours < kMinPeriodHours) hours = kMinPeriodHours;
        if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
        *periodSeconds = hours * 3600;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBulkIconPicker
//--------------------------------------------------------------------------------
// Display state before the user touches it: if every target already
// shares the exact same iconTexture (including "all empty", i.e. all
// using the plain dot), that shared value is shown selected. If they
// disagree, a "(mixed)" entry is shown instead - purely a status display,
// not a real choice: selecting any OTHER entry applies that choice to
// every target, and "(mixed)" naturally drops out of the list once the
// state resolves to non-mixed.
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
    if (mixed) iconLabels.push_back("(mixed)");
    iconLabels.push_back("Dot");
    for (const auto& fn : iconFiles)
        iconLabels.push_back(fn.c_str());

    //_ "Dot"'s index is 0 normally, or 1 if "(mixed)" occupies slot 0; a
    // real filename's index is offset by those lead entries ahead of it.
    int dotIndex = mixed ? 1 : 0;
    int iconIndex = mixed ? 0 : dotIndex;
    if (!mixed && !shared.empty())
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == shared)
                iconIndex = dotIndex + 1 + k;

    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo(label, &iconIndex, iconLabels.data(), (int)iconLabels.size()))
    {
        //_ Combo only returns true when the result differs from what was
        // passed in, so iconIndex can't still be "(mixed)" here.
        std::string newIcon = (iconIndex == dotIndex) ? std::string() : iconFiles[iconIndex - dotIndex - 1];
        for (int idx : targetIndices)
            g_Events[idx].iconTexture = newIcon;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsDuplicateEventName / IsDuplicateGroupName / IsDuplicateSlotKey
//--------------------------------------------------------------------------------
// Match the merge keys from events_storage.cpp: groups/events by name
// alone (GroupKey/EventKey), slots by name+offset together (SlotKey) -
// two slots can share a name at different offsets (Dry Top's two
// "Crash Site" slots) without colliding. selfIndex excludes the entry
// being checked from its own comparison.
//--------------------------------------------------------------------------------
bool IsDuplicateEventName(const std::vector<WorldEvent>& events, int selfIndex)
{
    const std::string& name = events[selfIndex].name;
    if (name.empty()) return false;
    for (int i = 0; i < (int)events.size(); i++)
        if (i != selfIndex && events[i].name == name)
            return true;
    return false;
}

bool IsDuplicateGroupName(const std::vector<CyclicGroup>& groups, int selfIndex)
{
    const std::string& name = groups[selfIndex].name;
    if (name.empty()) return false;
    for (int i = 0; i < (int)groups.size(); i++)
        if (i != selfIndex && groups[i].name == name)
            return true;
    return false;
}

bool IsDuplicateSlotKey(const std::vector<CyclicGroup::Slot>& slots, int selfIndex)
{
    const CyclicGroup::Slot& self = slots[selfIndex];
    if (self.name.empty()) return false;
    for (int i = 0; i < (int)slots.size(); i++)
        if (i != selfIndex && slots[i].name == self.name && slots[i].offset == self.offset)
            return true;
    return false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDuplicateWarning   (pairs with: IsDuplicateEventName/GroupName/SlotKey)
//--------------------------------------------------------------------------------
void DrawDuplicateWarning()
{
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[duplicate]");
}

//********************************************************************************
// DragPayload
//--------------------------------------------------------------------------------
// name   fixed-size copy of the dragged item's current name
//--------------------------------------------------------------------------------
// SetDragDropPayload copies a fixed-size raw blob - it has no idea about
// std::string, so the payload is a small POD struct with a fixed char[]
// buffer, matching the same nameBuf convention already used throughout
// this file for ImGui::InputText.
//--------------------------------------------------------------------------------
struct DragPayload
{
    char name[128];
};

//_ Two distinct type strings, not one shared type with a discriminator -
// AcceptDragDropPayload filters by type, rejecting cross-list drops for free.
const char* const kBasicEventDragType  = "WE_DRAG_BASIC_EVENT";
const char* const kCyclicGroupDragType = "WE_DRAG_CYCLIC_GROUP";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MakeDragSource / MakeDropTarget
//--------------------------------------------------------------------------------
// MakeDragSource: call right after the widget being dragged (e.g. a
// row's TreeNode). itemName is the event/group name to move on drop.
//
// MakeDropTarget: call right after the widget accepting a drop (a
// category header, or "drop here to uncategorize"). Performs the
// MoveCategoryMember() call itself; the bool return is informational.
//--------------------------------------------------------------------------------
void MakeDragSource(const char* dragType, const std::string& itemName)
{
    if (ImGui::BeginDragDropSource())
    {
        DragPayload payload{};
        strncpy(payload.name, itemName.c_str(), sizeof(payload.name) - 1);
        ImGui::SetDragDropPayload(dragType, &payload, sizeof(payload));
        ImGui::TextUnformatted(itemName.c_str()); //. preview text following the cursor
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
            MoveCategoryMember(categories, std::string(payload->name), targetCategoryIndex);
            dropped = true;
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribeCheckbox
//--------------------------------------------------------------------------------
// Meant to sit immediately before a TreeNode call, on the same line
// (SameLine), producing "[x] > TreeNode". A plain ImGui::Checkbox is
// noticeably taller than a TreeNode arrow, so FramePadding is zeroed
// just for this one call to match the arrow's height. Returns true if
// toggled this frame, same contract as ImGui::Checkbox itself.
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
// Hand-drawn glyphs via ImDrawList primitives - no bell/speaker glyph in
// the base font. `center` is the visual center, `size` roughly the full
// height in pixels; both authored in the same 24-unit box (s = size/24).
// DrawSpeakerIcon doubles as level 3's icon below and as a standalone
// label next to the sound-file picker.
//--------------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; //. 24-unit authoring box
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    //_ Dome + flare: a semicircle (shoulders on a diameter) over the top,
    // then straight lines flaring to the rim. Filled solid, not stroked.
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

    //_ Cone/flare: a trapezoid, drawn as its own convex piece since the
    // combined housing+cone silhouette isn't convex.
    dl->PathLineTo(P(11.0f, 9.0f));
    dl->PathLineTo(P(16.0f, 4.0f));
    dl->PathLineTo(P(16.0f, 20.0f));
    dl->PathLineTo(P(11.0f, 15.0f));
    dl->PathFillConvex(color);

    //_ Sound waves: two concentric arcs, stroked (a filled crescent this
    // small would read as a smudge rather than a wave).
    dl->PathArcTo(P(11.0f, 12.0f), 5.0f * s, -0.65f, 0.65f, 8);
    dl->PathStroke(color, false, 1.4f * s);

    dl->PathArcTo(P(11.0f, 12.0f), 8.5f * s, -0.55f, 0.55f, 8);
    dl->PathStroke(color, false, 1.4f * s);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelIcon
//--------------------------------------------------------------------------------
// 4-way control: level 0 unsubscribed ("+"), 1 subscribed/silent (bell),
// 2 +toast ("-"), 3 +toast+sound (speaker) - see GetBasicEventNotifyLevel
// in subscriptions.h. Left-click advances one level, wrapping 3 -> 0;
// jumping to any level directly is in DrawNameAndContextMenu's right-click
// menu instead.
//
// Manual hit-test + ImDrawList (not a real widget), sized to
// GetFrameHeight() to match the tree arrow. Returns the level to apply
// this frame - unchanged unless this click just advanced it.
//--------------------------------------------------------------------------------
int DrawNotifyLevelIcon(const char* idSuffix, int level)
{
    ImGui::PushID(idSuffix);

    //_ Same reasoning as DrawSubscribeCheckbox: zero FramePadding so this
    // icon matches the tree arrow's height instead of a full frame.
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
    //_ was 0.28f, which kept the +/- lines shrunk well inside the box;
    // tightened so they now run edge-to-edge like the bell/speaker below.
    float pad = sq * 0.10f;

    switch (level)
    {
        case 0: //. unsubscribed - plain "+"
            dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
            dl->AddLine(ImVec2(center.x, rmin.y + pad), ImVec2(center.x, rmax.y - pad), col, 1.6f);
            break;
        case 1: //. subscribed, silent - bell
            DrawBellIcon(dl, center, sq * 0.96f, col);
            break;
        case 2: //. subscribed + toast - speaker
            DrawSpeakerIcon(dl, center, sq * 0.96f, col);
            break;
        //_ 3: subscribed + toast + sound - "-"; clicking wraps back to
        // level 0 (fully unsubscribed).
        default:
            dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
            break;
    }

    ImGui::Dummy(ImVec2(sq, sq));
    ImGui::PopStyleVar();

    int newLevel = level;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        newLevel = (level + 1) % 4;

    if (hovered)
    {
        ImGui::SetTooltip(
            level == 0 ? "Click to subscribe" :
            level == 1 ? "Subscribed — click to also show a toast notification\n(right-click the name for more options)" :
            level == 2 ? "Subscribed + toast notification — click to also play a sound\n(right-click the name for more options)"
                       : "Subscribed + toast + sound — click to unsubscribe\n(right-click the name for more options)");
    }

    ImGui::PopID();
    return newLevel;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDragButton
//--------------------------------------------------------------------------------
// Small button placed next to the Location field that arms/disarms
// map-drag edit mode for one Basic Event or Cyclic Group (see EditModeState
// in maprender.h). Reads "Drag" when this row isn't the one currently being
// edited, and "Stop" when it is - clicking it toggles. A hovered tooltip
// explains the interaction either way, since "Drag"/"Stop" alone doesn't
// say WHERE to actually drag it (the marker on the map, not this button).
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
        if (isBeingEdited)
            ImGui::TextUnformatted("Click to stop dragging on the map.");
        else
            ImGui::TextUnformatted("Click, then left-click-drag this marker\non the map to reposition it.");
        ImGui::EndTooltip();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNameAndContextMenu
//--------------------------------------------------------------------------------
// Shared expand/collapse + name + right-click row, used by Basic Events,
// Cyclic Groups, Cyclic slots, and both category lists. Right-click always
// offers Edit name / Delete; toggleDone and notifyLevel/setNotifyLevel add
// optional entries, left null/-1 where not applicable (e.g. categories
// pass neither).
//
// editBuffers is the caller's own edit-in-progress map, keyed by editKey
// (kept separate from removeIndex since slots share one map across
// groups - see DrawCyclicGroupRow). Returns {open, newName}; sets
// pendingRemoveIndex = removeIndex on Delete.
//--------------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(const char* treeNodeId, int editKey, int removeIndex, const std::string& currentName, std::map<int, std::string>& editBuffers, int& pendingRemoveIndex, const char* dragType, const char* autoTag, std::function<void()> toggleDone,
    int notifyLevel, std::function<void(int)> setNotifyLevel)
{
    std::string label = currentName.empty() ? "(unnamed)" : currentName;
    if (autoTag)
    {
        label += " ";
        label += autoTag; //. display-only
    }
    bool open = ImGui::TreeNode(treeNodeId, "%s", label.c_str());
    if (autoTag && ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatically tracked via the GW2 API.\n"
                          "Drops off the Subscriptions bar/window on its own\n"
                          "once claimed today (no need to check it off by hand).");

    //_ Drag source is optional: categories are drop targets only, so
    // they pass dragType = nullptr and this is skipped.
    if (dragType)
        MakeDragSource(dragType, currentName);

    if (ImGui::BeginPopupContextItem("##name_context_menu"))
    {
        if (toggleDone)
        {
            if (ImGui::MenuItem("Mark done for today"))
                toggleDone();
            ImGui::Separator();
        }
        if (setNotifyLevel && notifyLevel >= 0)
        {
            //_ Jump menu (not just a shortcut past the forward-only
            // cycle); current stage shows a checkmark, not hidden.
            if (ImGui::MenuItem("Set to: Subscribed + Toast + Sound", nullptr, notifyLevel == 3))
                setNotifyLevel(3);
            if (ImGui::MenuItem("Set to: Subscribed + Toast", nullptr, notifyLevel == 2))
                setNotifyLevel(2);
            if (ImGui::MenuItem("Set to: Subscribed only", nullptr, notifyLevel == 1))
                setNotifyLevel(1);
            if (ImGui::MenuItem("Set to: Unsubscribed", nullptr, notifyLevel == 0))
                setNotifyLevel(0);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Edit name"))
            editBuffers[editKey] = currentName; //. seeded when edit starts
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
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

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("##inline_name_edit", buf, sizeof(buf)))
        it->second = buf; //. persists into next frame

    ImGui::SameLine();
    if (ImGui::SmallButton("Save##name_edit_save"))
    {
        std::string saved = it->second;
        editBuffers.erase(it);
        return { open, saved };
    }

    return { open, currentName }; //. unchanged until Save is clicked
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContainsCaseInsensitive / EventMatchesSearch / GroupMatchesSearch
//--------------------------------------------------------------------------------
// One shared query filters both Basic Events and Cyclic Events at once -
// a single search box, not two.
//
// Matching is case-insensitive substring, and for Cyclic Events checks
// BOTH the group's own name AND every one of its slot names - so typing
// "Crash Site" finds Dry Top even though "Dry Top" itself doesn't contain
// that text.
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
    return ContainsCaseInsensitive(ev.name, queryLower);
}

bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower)
{
    if (ContainsCaseInsensitive(grp.name, queryLower)) return true;
    for (const auto& slot : grp.slots)
        if (ContainsCaseInsensitive(slot.name, queryLower)) return true;
    return false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicEventRow   (pairs with: DrawCyclicGroupRow)
//--------------------------------------------------------------------------------
// Draws one g_Events[i] row in full - extracted out of the main loop so
// both the "uncategorized" pass and each category's "members" pass can
// call the same drawing code. PushID/PopID is the CALLER's responsibility
// (the same index is drawn from different places depending on category
// membership). Sets pendingRemoveIndex = i on remove; does not modify
// g_Events directly.
//--------------------------------------------------------------------------------
void DrawBasicEventRow(int i, int& pendingRemoveIndex)
{
    //_ Tracks in-edit-mode indices - see DrawNameAndContextMenu. Shared
    // (function-static) across category and uncategorized passes alike.
    static std::map<int, std::string> editingNames;

    WorldEvent& ev = g_Events[i];

    //_ Drawn before the name/tree-arrow, same slot DrawSubscribeCheckbox
    // used to occupy. See subscriptions.h for what each level touches.
    int notifyLevel = GetBasicEventNotifyLevel(ev.name);
    int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.name, newNotifyLevel);
    ImGui::SameLine();

    //_ Map-only show/hide; doesn't touch the Subscriptions bar/window
    // (that's the checkbox above). ev.shown defaults to true.
    DrawSubscribeCheckbox("##show_on_map", ev.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show on the map overlay\n(Subscriptions bar/window are unaffected)");
    ImGui::SameLine();

    std::string oldName = ev.name;
    NameRowResult nameResult = DrawNameAndContextMenu("##event_node", i, i, ev.name, editingNames, pendingRemoveIndex, kBasicEventDragType,
        ev.apiWorldBossId.empty() ? nullptr : "(auto)",
        [&ev]() { ToggleBasicEventDoneToday(ev.name); },
        notifyLevel, [&ev](int lvl) { SetBasicEventNotifyLevel(ev.name, lvl); });
    bool open = nameResult.open;
    if (nameResult.newName != oldName)
    {
        ev.name = nameResult.newName;
        RenameCategoryMember(g_BasicCategories, oldName, ev.name);
        RenameSubscribedBasicEvent(oldName, ev.name);
    }

    if (IsDuplicateEventName(g_Events, i))
        DrawDuplicateWarning();

    if (open)
    {
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputFloat2("Location", &ev.continentX, "%.0f");

        ImGui::SameLine();
        {
            char idSuffix[16];
            snprintf(idSuffix, sizeof(idSuffix), "be%d", i);
            DrawDragButton(EditTarget::BasicEvent, i, idSuffix);
        }

        ImGui::SetNextItemWidth(50.0f);
        int durationMinutes = ev.duration / 60;
        if (ImGui::InputInt("Duration (min)", &durationMinutes,0,0))
        {
            if (durationMinutes < 1) durationMinutes = 1;
            ev.duration = durationMinutes * 60;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Varying", &ev.isVarying);

        if (ev.isVarying)
        {
            //_ Sorted list of HH:MM start times. Labeled UTC and not
            // auto-converted - the schedule is UTC by design.
            ImGui::Spacing();
            ImGui::TextUnformatted("Times (UTC)");
            ImGui::SameLine();
            bool pendingAddTime = ImGui::SmallButton("+##add_time");

            int pendingRemoveTimeIndex = -1;

            for (int t = 0; t < (int)ev.varyingTimes.size(); t++)
            {
                ImGui::PushID(t);

                int hour   = ev.varyingTimes[t] / 3600;
                int minute = (ev.varyingTimes[t] % 3600) / 60;

                //_ Narrow, unlabeled fields (":" between them reads as a
                // clock) so hour+minute+remove all fit on one row.
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

            //_ Re-sorted every frame (not conditionally) since
            // GetSecondsUntilEventStart() requires ascending order.
            std::sort(ev.varyingTimes.begin(), ev.varyingTimes.end());
        }
        else
        {
            ImGui::SetNextItemWidth(50.0f);
            int offsetMinutes = ev.offset / 60;
            if (ImGui::InputInt("Offset (min)", &offsetMinutes, 0, 0))
            {
                if (offsetMinutes < 0) offsetMinutes = 0;
                ev.offset = offsetMinutes * 60;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(50.0f);
            DrawPeriodHoursDragInt(&ev.period);
        }

        //_ "Dot" (index 0) keeps the plain circle; any other entry names
        // a textures/ file, tinted to the status color (see maprender.cpp).
        const std::vector<std::string>& iconFiles = GetEventIconFilenames();
        std::vector<const char*> iconLabels;
        iconLabels.push_back("Dot");
        for (const auto& fn : iconFiles)
            iconLabels.push_back(fn.c_str());

        int iconIndex = 0; //. "Dot"
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == ev.iconTexture)
                iconIndex = k + 1;

        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("Icon", &iconIndex, iconLabels.data(), (int)iconLabels.size()))
            ev.iconTexture = (iconIndex == 0) ? std::string() : iconFiles[iconIndex - 1];

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##icon_rescan"))
            ScanEventIconFiles();

        //_ Free-text chat/map code for the copy-to-clipboard button; not
        // a merge key, so no Save-button buffering like the name field.
        {
            char chatCodeBuf[128];
            strncpy(chatCodeBuf, ev.chatCode.c_str(), sizeof(chatCodeBuf) - 1);
            chatCodeBuf[sizeof(chatCodeBuf) - 1] = '\0';

            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::InputText("Text to copy##chat_code", chatCodeBuf, sizeof(chatCodeBuf)))
                ev.chatCode = chatCodeBuf;
        }

        ImGui::TreePop();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawCyclicGroupRow   (pairs with: DrawBasicEventRow)
//--------------------------------------------------------------------------------
// Draws one g_CyclicGroups[i] row in full (header, name, location, period,
// colors, idle-color override, and the nested per-slot list) - extracted
// the same way as DrawBasicEventRow, so both the "uncategorized" pass and
// each category's "members" pass can call the identical drawing code.
//
// PushID/PopID for this row are the CALLER's responsibility, same as
// DrawBasicEventRow.
//
// Sets pendingRemoveGroupIndex = i if this row's remove button was
// clicked this frame; does not modify g_CyclicGroups directly.
//--------------------------------------------------------------------------------
void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex)
{
    static std::map<int, std::string> editingNames;

    CyclicGroup& grp = g_CyclicGroups[i];

    //_ Bulk convenience over per-slot subscriptions - no storage of its
    // own. Checked only if every slot is subscribed; mixed reads unchecked.
    bool allSlotsSubscribed = !grp.slots.empty() &&
        std::all_of(grp.slots.begin(), grp.slots.end(), [&](const CyclicGroup::Slot& slot)
        {
            return IsCyclicSlotSubscribed(CyclicSubscriptionKey{ grp.name, slot.offset });
        });
    if (DrawSubscribeCheckbox("##subscribe_group", allSlotsSubscribed))
    {
        for (const auto& slot : grp.slots)
        {
            CyclicSubscriptionKey key{ grp.name, slot.offset };
            //_ allSlotsSubscribed already holds the post-click state:
            // unticking drops every slot to 0; ticking only raises 0 -> 1.
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
        ImGui::SetTooltip("Subscribe/unsubscribe every occurrence in this cycle at once\n(checked only when all of them already are)");
    ImGui::SameLine();

    //_ Show/hide the ENTIRE ring (track + every slot) - see
    // CyclicGroup::shown in events.h.
    DrawSubscribeCheckbox("##show_group_on_map", grp.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show/hide this entire ring on the map overlay\n(no circle drawn at all while unchecked)");
    ImGui::SameLine();

    std::string oldGroupName = grp.name;
    NameRowResult nameResult = DrawNameAndContextMenu("##group_node", i, i, grp.name, editingNames, pendingRemoveGroupIndex, kCyclicGroupDragType,
        grp.apiMapChestId.empty() ? nullptr : "(auto)");
    bool open = nameResult.open;
    if (nameResult.newName != oldGroupName)
    {
        grp.name = nameResult.newName;
        RenameCategoryMember(g_CyclicCategories, oldGroupName, grp.name);
    }

    if (IsDuplicateGroupName(g_CyclicGroups, i))
        DrawDuplicateWarning();

    if (open)
    {
        //_ Compact row: Location, Period, Color, Idle override all share
        // one line. Swatches use NoInputs (small square, full picker on click).
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputFloat2("Location", &grp.continentX, "%.0f");

        ImGui::SameLine();
        {
            char idSuffix[16];
            snprintf(idSuffix, sizeof(idSuffix), "cg%d", i);
            DrawDragButton(EditTarget::CyclicGroup, i, idSuffix);
        }

        ImGui::SetNextItemWidth(50.0f);
        DrawPeriodHoursDragInt(&grp.period);

        //_ Base color - colors.base is a plain ImVec4, so ColorEdit4 binds
        // to it directly; no read/convert/write-back round trip needed.
        ImGui::ColorEdit4("Color", &grp.colors.base.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

        //_ Optional override: unchecked uses colors.ter() (see
        // CyclicGroup::IdleColor()); checked stores an explicit ImU32.
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
            if (ImGui::ColorEdit4("Custom Color##group", &idleColorVec.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel) && hasCustomIdle)
                grp.idleColor = ColorU32(idleColorVec);
        }

        //_ Slots: the individual events within this cycle. Same deferred
        // add/remove pattern, nested one PushID level deeper (group i, slot s).
        ImGui::Spacing();
        ImGui::TextUnformatted("Events");
        ImGui::SameLine();
        bool pendingAddSlot = ImGui::SmallButton("+##add_slot");

        int pendingRemoveSlotIndex = -1;

        //_ Shared across every group (function-static, not per-loop) -
        // keyed by (group i, slot s) so slot 0 in different groups can't collide.
        static std::map<int, std::string> editingSlotNames;

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            CyclicGroup::Slot& slot = grp.slots[s];
            ImGui::PushID(s);

            //_ Per SLOT, not per group - the group checkbox above is a
            // bulk convenience over these same per-slot subscriptions.
            CyclicSubscriptionKey subKey{ grp.name, slot.offset };
            int notifyLevel = GetCyclicSlotNotifyLevel(subKey);
            int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
            if (newNotifyLevel != notifyLevel)
                SetCyclicSlotNotifyLevel(subKey, newNotifyLevel);
            ImGui::SameLine();

            //_ Show/hide just THIS occurrence - the rest of the ring
            // still draws. See CyclicGroup::Slot::shown in events.h.
            DrawSubscribeCheckbox("##show_slot_on_map", slot.shown);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show/hide this occurrence on the map overlay");
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            NameRowResult slotNameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, slot.name, editingSlotNames, pendingRemoveSlotIndex,
                nullptr, nullptr, [subKey]() { ToggleCyclicSlotDoneToday(subKey); },
                notifyLevel, [subKey](int lvl) { SetCyclicSlotNotifyLevel(subKey, lvl); });
            bool slotOpen = slotNameResult.open;
            //_ Slots aren't categorized, and subscriptions key on
            // (group name, offset), not name - so no rename fixups needed.
            if (slotNameResult.newName != slot.name)
                slot.name = slotNameResult.newName;

            if (IsDuplicateSlotKey(grp.slots, s))
                DrawDuplicateWarning();

            if (slotOpen)
            {
                ImGui::SetNextItemWidth(50.0f);
                int offsetMinutes = slot.offset / 60;
                if (ImGui::DragInt("Offset", &offsetMinutes, 0, 0, 0, "%dmin"))
                {
                    if (offsetMinutes < 0) offsetMinutes = 0;
                    slot.offset = offsetMinutes * 60;
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                int durationMinutes = slot.duration / 60;
                if (ImGui::DragInt("Duration (min)", &durationMinutes, 0, 0, 0, "%dmin"))
                {
                    if (durationMinutes < 1) durationMinutes = 1;
                    slot.duration = durationMinutes * 60;
                }

                //_ Repeat must evenly divide the period; snaps down to
                // the nearest divisor of the CURRENT period (re-checked every frame).
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                int repeatInput = slot.repeat;
                if (ImGui::InputInt("Repetition", &repeatInput, 0, 0))
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
                Tooltip("How often the event repeats in the set period.\n"
                        "Has to fit perfectly, if not possible make a second entry instead.\n"
                        "Example: Event repeats exactly every hour. So 2 repeats in a 2h period.");

                ImGui::SetNextItemWidth(100.0f);
                static const char* const kTierLabels[] = { "Primary", "Secondary", "Tertiary" };
                int tierIndex = (int)slot.tier;
                if (ImGui::Combo("Tier", &tierIndex, kTierLabels, 3))
                    slot.tier = (ColorTier)tierIndex;

                //_ Same checkbox-gates-swatch pattern as Custom Idle
                // above; seeded from the slot's current resolved color.
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
                    if (ImGui::ColorEdit4("Custom Color##slot", &slotColorVec.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel) && hasCustomColor)
                        slot.customColor = ColorU32(slotColorVec);
                }

                //_ Same as WorldEvent::chatCode; not a merge key, so
                // live-edits directly with no Save-button buffering.
                {
                    char chatCodeBuf[128];
                    strncpy(chatCodeBuf, slot.chatCode.c_str(), sizeof(chatCodeBuf) - 1);
                    chatCodeBuf[sizeof(chatCodeBuf) - 1] = '\0';

                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText("Text to copy##slot_chat_code", chatCodeBuf, sizeof(chatCodeBuf)))
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
            CyclicGroup::Slot newSlot{};
            newSlot.name     = "New Event";
            newSlot.offset   = 0;
            newSlot.duration = 600; //. 10 min, a reasonable default
            newSlot.tier     = ColorTier::Primary;
            newSlot.repeat   = 1;
            grp.slots.push_back(newSlot);
        }

        ImGui::TreePop();
    }
}