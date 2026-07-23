// addon_options_helpers.cpp
// Implementations for everything declared in addon_options_helpers.h —
// the scoped-disable helper, period widget, bulk icon picker, color
// conversion, duplicate-name checks, drag-and-drop plumbing, hand-drawn
// glyphs, the notify-level control, the shared name/context-menu row,
// search predicates, and the two full row drawers (Basic Event / Cyclic
// Group). See addon_options.cpp for the panel that assembles these into
// the actual Nexus options UI.

#include "addon_options_helpers.h"
#include "color_utils.h"

#include "subscriptions.h"
#include "events_tracking.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled / PushItemFlag — not in the public header

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// ImGuiScopedDisabled
// ---------------------------------------------------------------------------
ImGuiScopedDisabled::ImGuiScopedDisabled(bool cond) : active(cond)
{
    if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
}

ImGuiScopedDisabled::~ImGuiScopedDisabled()
{
    if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
}

// ---------------------------------------------------------------------------
// Period field
// ---------------------------------------------------------------------------
// Whole hours only, 1-12h, deliberately — no GW2 event or event chain
// runs on anything other than a whole-hour cycle, so this keeps the
// field from accepting values that imply a typo (e.g. half an hour).
// Drawn as a DragInt rather than a Combo so raising the cap doesn't
// require growing a hardcoded label array — 12h covers every period
// seen so far (including the 7h groups) with headroom.
// ---------------------------------------------------------------------------

// Converts a period in seconds to whole hours, clamped to
// [kMinPeriodHours, kMaxPeriodHours]. Any out-of-range or non-whole-hour
// value (which shouldn't occur from this UI, but could from a hand-edited
// JSON file) just snaps to the nearest valid hour rather than crashing or
// showing garbage.
int PeriodSecondsToHours(int periodSeconds)
{
    int hours = periodSeconds / 3600;
    if (hours < kMinPeriodHours) hours = kMinPeriodHours;
    if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
    return hours;
}

// Draws the shared "Period" DragInt widget and writes the result (in
// seconds) back through periodSeconds if the user changed it.
void DrawPeriodHoursDragInt(int* periodSeconds)
{
    int hours = PeriodSecondsToHours(*periodSeconds);
    if (ImGui::DragInt("Period", &hours, 0.1f, kMinPeriodHours, kMaxPeriodHours, "%dh"))
    {
        // DragInt's min/max only clamp the drag gesture itself — typing a
        // value directly (ctrl+click to turn it into a text box) can still
        // enter something outside [min, max], so clamp again explicitly
        // rather than trusting the widget's own bounds.
        if (hours < kMinPeriodHours) hours = kMinPeriodHours;
        if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
        *periodSeconds = hours * 3600;
    }
}

// ---------------------------------------------------------------------------
// DrawBulkIconPicker
// ---------------------------------------------------------------------------
// Display state before the user touches it: if every target already
// shares the exact same iconTexture (including "all empty", i.e. all
// using the plain dot), that shared value is shown selected. If they
// disagree, a "(mixed)" entry is shown instead — purely a status display,
// not a real choice: selecting any OTHER entry applies that choice to
// every target, and "(mixed)" naturally drops out of the list once the
// state resolves to non-mixed.
// ---------------------------------------------------------------------------
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

    // "Dot"'s index is 0 normally, or 1 if "(mixed)" occupies slot 0; a
    // real filename's index is offset by however many of those two lead
    // entries exist ahead of it.
    int dotIndex = mixed ? 1 : 0;
    int iconIndex = mixed ? 0 : dotIndex;
    if (!mixed && !shared.empty())
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == shared)
                iconIndex = dotIndex + 1 + k;

    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo(label, &iconIndex, iconLabels.data(), (int)iconLabels.size()))
    {
        // By the time this branch runs, ImGui has already confirmed the
        // SELECTION changed, so iconIndex can't still be pointing at
        // "(mixed)" (index 0 while mixed==true) — Combo only returns true
        // when the result differs from what was passed in, and "(mixed)"
        // was what was passed in for that case.
        std::string newIcon = (iconIndex == dotIndex) ? std::string() : iconFiles[iconIndex - dotIndex - 1];
        for (int idx : targetIndices)
            g_Events[idx].iconTexture = newIcon;
    }
}

// ---------------------------------------------------------------------------
// Duplicate-name warnings
// ---------------------------------------------------------------------------
// These match the actual merge keys used in events_storage.cpp, not just
// "is this name used elsewhere" in general. Groups and events are
// matched by name alone (GroupKey/EventKey), so a plain duplicate name
// genuinely causes merge ambiguity. Slots, however, are matched by
// name+offset together (SlotKey) — two slots sharing a name at DIFFERENT
// offsets (Dry Top's two "Crash Site" slots) is normal and intentional,
// so the slot-level check only flags a collision when BOTH name and
// offset match.
//
// Both take the index of the entry being checked so they can exclude it
// from the comparison — otherwise every entry would trivially "collide"
// with itself.
// ---------------------------------------------------------------------------
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

void DrawDuplicateWarning()
{
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[duplicate]");
}

// ---------------------------------------------------------------------------
// Drag-and-drop: moving an item into/out of a category
// ---------------------------------------------------------------------------
// SetDragDropPayload copies a fixed-size raw blob — it has no idea about
// std::string, so the payload is a small POD struct with a fixed char[]
// buffer, matching the same nameBuf convention already used throughout
// this file for ImGui::InputText.
//
// Two distinct payload TYPE STRINGS ("WE_DRAG_BASIC_EVENT" /
// "WE_DRAG_CYCLIC_GROUP") rather than one shared type with a discriminator
// field — AcceptDragDropPayload filters by type string, so a Basic Event
// dragged over a Cyclic category's drop target is rejected at the API
// level automatically. This gives "one list, no mixing" for free, without
// the drop-target code needing to manually check which list a payload
// came from.
// ---------------------------------------------------------------------------
struct DragPayload
{
    char name[128];
};

const char* const kBasicEventDragType  = "WE_DRAG_BASIC_EVENT";
const char* const kCyclicGroupDragType = "WE_DRAG_CYCLIC_GROUP";

// Call right after the widget that should act as the thing being dragged
// (e.g. right after drawing a row's TreeNode). itemName is whatever
// should be moved if this drag ends in a drop — i.e. the event's or
// group's current name.
void MakeDragSource(const char* dragType, const std::string& itemName)
{
    if (ImGui::BeginDragDropSource())
    {
        DragPayload payload{};
        strncpy(payload.name, itemName.c_str(), sizeof(payload.name) - 1);
        ImGui::SetDragDropPayload(dragType, &payload, sizeof(payload));
        ImGui::TextUnformatted(itemName.c_str()); // preview text following the cursor
        ImGui::EndDragDropSource();
    }
}

// Call right after the widget that should accept a drop (a category
// header, or the section header for "drop here to uncategorize").
// Performs the actual MoveCategoryMember() call itself when a matching
// payload is dropped — callers don't need to do anything with the return
// value, but it's returned anyway in case a caller wants to react (e.g.
// nothing currently does, but it costs nothing to expose).
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

// ---------------------------------------------------------------------------
// DrawSubscribeCheckbox
// ---------------------------------------------------------------------------
// Meant to sit immediately BEFORE a TreeNode call, on the same line —
// i.e. the caller does:
//
//   bool subscribed = ...;
//   if (DrawSubscribeCheckbox("##watch_x", subscribed)) Toggle...(...);
//   ImGui::SameLine();
//   NameRowResult nameResult = DrawNameAndContextMenu(...);
//
// producing "[x] > TreeNode" rather than the arrow/label first.
//
// A plain ImGui::Checkbox is noticeably taller than a TreeNode arrow,
// since Checkbox draws a full button-style frame using the theme's
// FramePadding. Zeroing FramePadding just for this one call (pushed/
// popped tightly around it, not left active for the rest of the row)
// makes the checkbox's height match the tree arrow's, keeping row height
// consistent.
//
// Returns true if the checkbox was toggled this frame (same contract as
// ImGui::Checkbox itself) — callers still own actually flipping the
// underlying subscription state.
// ---------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleVar();
    return changed;
}

// ---------------------------------------------------------------------------
// DrawBellIcon
// ---------------------------------------------------------------------------
// A minimal, hand-drawn bell glyph — built from ImDrawList primitives
// rather than a font glyph, since the base font here only covers Basic
// Latin plus a handful of general-purpose symbols; there's no bell
// character available without pulling in a whole icon font for one
// row-glyph.
//
// ONE shared function, drawn fresh at whatever position/size/color each
// caller needs — not a baked texture, and not a separate copy of this
// geometry per event. `center` is the icon's visual center; `size` is
// roughly its full height in screen pixels.
//
// Filled solid rather than stroked outline: at the ~13px this actually
// renders at inside a tree row, a thin stroke gets fuzzy while a solid
// silhouette stays crisp. The small hanging "clapper" circle is what
// keeps the shape reading as a bell rather than just a dome once it's
// down at that size.
// ---------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; // geometry below is authored in a 24-unit box
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    // Dome + flare: a semicircle (the two shoulder points sit exactly on
    // a diameter, so this is precisely 180 degrees, not an arbitrary
    // arc) from the left shoulder over the top to the right shoulder,
    // then straight lines flaring out to a wider bottom rim. Filling
    // implicitly closes the path back to its first point, same as the
    // implicit "Z" this was prototyped as in SVG.
    dl->PathArcTo(P(12.0f, 14.0f), 6.0f * s, IM_PI, IM_PI * 2.0f, 12);
    dl->PathLineTo(P(20.0f, 18.0f));
    dl->PathLineTo(P(4.0f, 18.0f));
    dl->PathFillConvex(color);

    dl->AddCircleFilled(P(12.0f, 20.4f), 1.3f * s, color, 12);
}

// ---------------------------------------------------------------------------
// DrawSpeakerIcon
// ---------------------------------------------------------------------------
// Same hand-drawn-glyph approach as DrawBellIcon right above (own comment
// there explains why: no bell/speaker character in the base font, one
// shared function rather than a baked texture or a per-caller copy),
// authored in the same 24-unit box so it drops in cleanly wherever a
// notify-level icon needs one. Used two places: as level 3's icon in
// DrawNotifyLevelIcon's cycle below, and as a plain static label glyph
// next to the sound-file picker in the options panel.
//
// Drawn as two separate filled pieces rather than one PathFillConvex
// call: the driver housing (a plain rect) plus the flared cone together
// form a concave hexagon (notches where the narrow housing meets the
// wider flare), and PathFillConvex — same as DrawBellIcon's dome —
// requires an actually convex outline. The two pieces are drawn slightly
// overlapping along their shared edge to avoid a thin antialiasing seam
// between them.
//
// The two sound-wave arcs are stroked, not filled, matching the +/-
// glyphs' stroke convention in DrawNotifyLevelIcon below — a filled
// crescent this small reads as a smudge rather than a wave.
// ---------------------------------------------------------------------------
void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; // same 24-unit authoring box as DrawBellIcon
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    // Driver housing — extends to x=11.5 rather than the cone's nominal
    // x=11 start, purely to close the antialiasing seam mentioned above.
    dl->AddRectFilled(P(5.0f, 9.0f), P(11.5f, 15.0f), color);

    // Cone/flare — a trapezoid, convex on its own even though the
    // combined housing+cone silhouette isn't.
    dl->PathLineTo(P(11.0f, 9.0f));
    dl->PathLineTo(P(16.0f, 4.0f));
    dl->PathLineTo(P(16.0f, 20.0f));
    dl->PathLineTo(P(11.0f, 15.0f));
    dl->PathFillConvex(color);

    // Sound waves — two concentric right-opening arcs, small enough to
    // stay legible once this shrinks down to a tree-row-sized icon.
    dl->PathArcTo(P(11.0f, 12.0f), 5.0f * s, -0.65f, 0.65f, 8);
    dl->PathStroke(color, false, 1.4f * s);

    dl->PathArcTo(P(11.0f, 12.0f), 8.5f * s, -0.55f, 0.55f, 8);
    dl->PathStroke(color, false, 1.4f * s);
}

// ---------------------------------------------------------------------------
// DrawNotifyLevelIcon
// ---------------------------------------------------------------------------
// Replaces the plain subscribe checkbox with a single 4-way control:
//   level 0 — unsubscribed                    — shown as "+"
//   level 1 — subscribed, silent              — shown as a bell
//   level 2 — subscribed + toast popup        — shown as a "-"
//   level 3 — subscribed + toast + sound      — shown as a speaker
// (see GetBasicEventNotifyLevel/SetBasicEventNotifyLevel in
// subscriptions.h for the underlying three-list state this reads/writes).
//
// Left-click always advances exactly ONE level, wrapping 3 -> 0 — the
// clean, common-case gesture. Jumping DOWN a level, or straight to
// unsubscribed without cycling through every step, lives in this row's
// right-click context menu instead (see DrawNameAndContextMenu's
// notifyLevel/setNotifyLevel parameters) rather than as a second gesture
// here — that menu already exists for "Edit name", so this reuses it
// rather than teaching a new interaction.
//
// Manual hit-test + ImDrawList rather than a real widget, same reasoning
// as subscriptions_window.cpp's DrawSubscriptionRow: a fixed square
// slot, sized to GetFrameHeight() so it lines up with the tree arrow /
// DrawSubscribeCheckbox's own tightened height on the same row.
//
// Returns the level to actually apply this frame: unchanged unless this
// exact click just advanced it, so callers do:
//   int level = DrawNotifyLevelIcon("##notify_x", GetBasicEventNotifyLevel(name));
//   if (level != GetBasicEventNotifyLevel(name)) SetBasicEventNotifyLevel(name, level);
// ---------------------------------------------------------------------------
int DrawNotifyLevelIcon(const char* idSuffix, int level)
{
    ImGui::PushID(idSuffix);

    // Same reasoning as DrawSubscribeCheckbox right above: GetFrameHeight()
    // pulls in the theme's full FramePadding, which is noticeably taller
    // than the tree arrow this sits next to. Zeroing it here too — not
    // just leaving it to whatever the last checkbox happened to push —
    // is what actually keeps this icon's box the same size/baseline as
    // "show on map" right next to it on the same line.
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
    float pad = sq * 0.10f; // was 0.28f — kept the +/- lines shrunk well inside
                            // the box; tightened so they now run edge-to-edge
                            // like the bell/speaker do below

    switch (level)
    {
        case 0: // unsubscribed — plain "+"
            dl->AddLine(ImVec2(rmin.x + pad, center.y), ImVec2(rmax.x - pad, center.y), col, 1.6f);
            dl->AddLine(ImVec2(center.x, rmin.y + pad), ImVec2(center.x, rmax.y - pad), col, 1.6f);
            break;
        case 1: // subscribed, silent — bell
            DrawBellIcon(dl, center, sq * 0.96f, col);
            break;
        case 2: // subscribed + toast — speaker
            DrawSpeakerIcon(dl, center, sq * 0.96f, col);
            break;
        default: // 3: subscribed + toast + sound — "-"; clicking
                 // this wraps back to level 0 (fully unsubscribed)
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

// ---------------------------------------------------------------------------
// DrawDragButton
// ---------------------------------------------------------------------------
// Small button placed next to the Location field that arms/disarms
// map-drag edit mode for one Basic Event or Cyclic Group (see EditModeState
// in maprender.h). Reads "Drag" when this row isn't the one currently being
// edited, and "Stop" when it is — clicking it toggles. A hovered tooltip
// explains the interaction either way, since "Drag"/"Stop" alone doesn't
// say WHERE to actually drag it (the marker on the map, not this button).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// DrawNameAndContextMenu
// ---------------------------------------------------------------------------
// Draws a row's expand/collapse TreeNode WITH its name as the node's own
// label (keeps the full row hoverable/clickable, not just the arrow
// glyph). Right-clicking opens a popup with "Edit name" and "Delete" —
// "Edit name" reveals an inline InputText + Save button next to the label
// (SameLine) rather than replacing it.
//
// toggleDone: optional callback wired to
// ToggleBasicEventDoneToday/ToggleCyclicSlotDoneToday (see
// events_tracking.h). When set, an extra "Mark done for today" entry is
// added above "Edit name", mirroring the same right-click menu already on
// the row in the Subscriptions window/bar/toast — same plain toggle, no
// checkmark/label change, since those don't show one either. Left as
// nullptr (the default) for rows with no "done" concept of their own:
// Cyclic Groups (the group as a whole isn't a single trackable
// occurrence — only its slots are) and both category rows.
//
// editBuffers is a per-context std::map<int, std::string> the caller owns
// (one each for Basic Events, Cyclic Groups, Basic Categories, Cyclic
// Categories), keyed by index. The map entry IS the in-progress edit
// text — seeded once when editing starts, then left alone every frame
// (NOT re-synced from currentName, which would overwrite an in-progress
// edit before Save saw it).
//
// editKey and removeIndex are separate parameters because slots share one
// editBuffers map across every group (see DrawCyclicGroupRow), so the
// edit-tracking key has to combine the group index too, while
// pendingRemoveSlotIndex still needs the bare slot index for
// grp.slots.erase(...). Every other call site just passes the same value
// for both.
//
// Returns {open, newName}: open is the TreeNode's expand/collapse state;
// newName is the possibly-edited name, which the caller assigns back into
// ev.name/grp.name/cat.name and follows up on if needed (e.g.
// RenameCategoryMember). Sets pendingRemoveIndex = removeIndex if
// "Delete" was clicked.
//
// autoTag: optional display-only suffix (e.g. "(auto)") shown when the
// caller's event/group has a live GW2 API cross-reference
// (WorldEvent::apiWorldBossId or CyclicGroup::apiMapChestId). Purely
// cosmetic — never folded into newName, so it has no effect on saving,
// renaming, drag payloads, or search/category matching.
//
// notifyLevel/setNotifyLevel: optional (default -1/nullptr, meaning "not
// applicable to this row" — categories pass neither). When present, adds
// "Set to: ..." entries for jumping straight to ANY level in the ladder
// (see DrawNotifyLevelIcon's comment for why this lives here rather than
// as its own gesture) — all four stages are always listed, not just the
// ones below the current one, so this doubles as a full jump menu (up OR
// down) rather than only a shortcut past the left-click cycle's forward-
// only direction. The current stage shows a checkmark (MenuItem's
// `selected` flag) rather than being hidden or disabled — clicking it
// again is a harmless no-op.
// ---------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(const char* treeNodeId, int editKey, int removeIndex, const std::string& currentName, std::map<int, std::string>& editBuffers, int& pendingRemoveIndex, const char* dragType, const char* autoTag, std::function<void()> toggleDone,
    int notifyLevel, std::function<void(int)> setNotifyLevel)
{
    std::string label = currentName.empty() ? "(unnamed)" : currentName;
    if (autoTag)
    {
        label += " ";
        label += autoTag;
    }
    bool open = ImGui::TreeNode(treeNodeId, "%s", label.c_str());
    if (autoTag && ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatically tracked via the GW2 API.\n"
                          "Drops off the Subscriptions bar/window on its own\n"
                          "once claimed today (no need to check it off by hand).");

    // Drag source attaches to the TreeNode itself, right after it's
    // drawn — NOT after BeginPopupContextItem/the edit fields below,
    // since those are conditional and the row should stay draggable by
    // its name/header regardless of whether it's mid-rename. Optional:
    // categories aren't drag SOURCES (only drag TARGETS), so they pass
    // nullptr here and this is skipped entirely.
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
            editBuffers[editKey] = currentName; // seed once, on transition into edit mode only
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
        it->second = buf; // persists into NEXT frame via the map entry, not lost

    ImGui::SameLine();
    if (ImGui::SmallButton("Save##name_edit_save"))
    {
        std::string saved = it->second;
        editBuffers.erase(it);
        return { open, saved };
    }

    return { open, currentName }; // unchanged until Save is actually clicked
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
// One shared query filters both Basic Events and Cyclic Events at once —
// a single search box, not two.
//
// Matching is case-insensitive substring, and for Cyclic Events checks
// BOTH the group's own name AND every one of its slot names — so typing
// "Crash Site" finds Dry Top even though "Dry Top" itself doesn't contain
// that text.
// ---------------------------------------------------------------------------
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
{
    if (needleLower.empty()) return true; // empty query matches everything
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

// ---------------------------------------------------------------------------
// DrawBasicEventRow
// ---------------------------------------------------------------------------
// Draws one g_Events[i] row in full (header, name, location, duration,
// varying toggle, and either the periodic fields or the time-of-day
// list) — extracted out of the main loop so both the "uncategorized"
// pass and each category's "members" pass can call the identical
// drawing code, rather than duplicating it.
//
// PushID/PopID for this row are the CALLER's responsibility (it needs to
// wrap the row in a scope that's unique not just by index — since the
// same index can be drawn from different places depending on category
// membership — see DrawCategoryManager below).
//
// Sets pendingRemoveIndex = i if this row's remove button was clicked
// this frame; does not modify g_Events directly (the caller defers the
// actual erase to after every row has been drawn).
// ---------------------------------------------------------------------------
void DrawBasicEventRow(int i, int& pendingRemoveIndex)
{
    // Tracks which g_Events indices currently have their name in
    // edit mode — see DrawNameAndContextMenu's comment. Function-static
    // rather than a parameter since every call site for Basic Events
    // shares the same underlying index space and the same intent: "is
    // THIS event index currently being renamed," regardless of whether
    // it's being drawn from the category pass or the uncategorized pass
    // this frame.
    static std::map<int, std::string> editingNames;

    WorldEvent& ev = g_Events[i];

    // Notify-level icon drawn BEFORE the name/tree-arrow, on the same
    // line ("[icon] > Name") — same slot DrawSubscribeCheckbox used to
    // occupy. Cycling past level 0 doesn't affect map rendering or
    // timing at all (see subscriptions.h); it only adds/removes ev.name
    // from the watchlist window's list and, at level 2+, the toast list,
    // and, at level 3, the sound list too.
    int notifyLevel = GetBasicEventNotifyLevel(ev.name);
    int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
    if (newNotifyLevel != notifyLevel)
        SetBasicEventNotifyLevel(ev.name, newNotifyLevel);
    ImGui::SameLine();

    // Map-only show/hide toggle, same tightened-checkbox treatment as the
    // subscribe checkbox right before it (so both sit at the same
    // height as the tree arrow) — see DrawSubscribeCheckbox's comment.
    // Doesn't touch the Subscriptions bar/window at all; those are
    // opt-in via the checkbox above regardless of this one. Checked =
    // shown (ev.shown defaults to true), so an unmodified event always
    // starts with both checkboxes reading unambiguously: subscribe
    // unchecked, shown checked.
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
            // Varying branch: a sorted list of individual start times,
            // each entered as an HH:MM time-of-day picker. Labeled UTC
            // (not auto-detected/converted — the underlying schedule is
            // UTC by design, and a user in a half-hour-offset timezone
            // mentally translating their local clock when filling this
            // in is an accepted, minor inconvenience rather than
            // something the addon tries to silently correct for).
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

                // Fixed, narrow widths so two int fields + a remove
                // button reliably fit on one row regardless of panel
                // width — InputInt's default width was wide enough
                // that the remove button could scroll off the visible
                // edge. Labels are hidden ("##Hour"/"##Minute") since
                // the "Times (UTC)" header above already says what
                // this row is; a ":" between the two fields reads as
                // a clock without needing per-field text labels.
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
                ev.varyingTimes.push_back(0); // midnight UTC; user adjusts from there

            // Re-sort after any edit/add/remove, every frame — defensive
            // rather than conditional, since GetSecondsUntilEventStart() in
            // maprender.cpp assumes this list stays in ascending order
            // to correctly find the next upcoming time. Even a single
            // same-frame HH:MM edit can put an entry out of order
            // relative to its neighbors, not just add/remove.
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

        // Icon dropdown — "Dot" (index 0, the default) keeps the plain
        // colored circle; any other entry names a file in the addon's
        // textures/ folder, drawn instead, tinted to the same status
        // color the dot would use (source image must be gray/alpha —
        // see s_iconCache in maprender.cpp).
        const std::vector<std::string>& iconFiles = GetEventIconFilenames();
        std::vector<const char*> iconLabels;
        iconLabels.push_back("Dot");
        for (const auto& fn : iconFiles)
            iconLabels.push_back(fn.c_str());

        int iconIndex = 0; // "Dot"
        for (int k = 0; k < (int)iconFiles.size(); k++)
            if (iconFiles[k] == ev.iconTexture)
                iconIndex = k + 1;

        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("Icon", &iconIndex, iconLabels.data(), (int)iconLabels.size()))
            ev.iconTexture = (iconIndex == 0) ? std::string() : iconFiles[iconIndex - 1];

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##icon_rescan"))
            ScanEventIconFiles();

        // Chat/map code (e.g. "[&BIgIAAA=]") the user can paste in from
        // the game once, to power the clipboard-copy button on this
        // event elsewhere in the UI. Free text, no validation — an empty
        // value just means "no code set yet", same convention as
        // iconTexture's "empty = default" above. Live-editing straight
        // into ev.chatCode each frame (no Save-button buffering, unlike
        // the name field above) is safe here because chatCode is never
        // used as a merge/lookup key anywhere in events_storage.cpp.
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

// ---------------------------------------------------------------------------
// DrawCyclicGroupRow
// ---------------------------------------------------------------------------
// Draws one g_CyclicGroups[i] row in full (header, name, location, period,
// colors, idle-color override, and the nested per-slot list) — extracted
// the same way as DrawBasicEventRow, so both the "uncategorized" pass and
// each category's "members" pass can call the identical drawing code.
//
// PushID/PopID for this row are the CALLER's responsibility, same as
// DrawBasicEventRow.
//
// Sets pendingRemoveGroupIndex = i if this row's remove button was
// clicked this frame; does not modify g_CyclicGroups directly.
// ---------------------------------------------------------------------------
void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex)
{
    static std::map<int, std::string> editingNames;

    CyclicGroup& grp = g_CyclicGroups[i];

    // Group-level "subscribe all" checkbox — mirrors DrawBasicEventRow's
    // subscribe/shown pair for visual consistency, but unlike that pair
    // this one has no storage of its own. Per-slot subscription is still
    // the only real state (see CyclicSubscriptionKey in subscriptions.h)
    // — this checkbox just reads/writes ALL slots' subscriptions at once:
    //   - displayed checked only if EVERY slot is currently subscribed
    //     (an empty group reads unchecked, not vacuously checked)
    //   - ticking it subscribes every slot; unticking it unsubscribes
    //     every slot
    // Deliberately NOT tri-state: a "some but not all" mix just reads as
    // unchecked here, same as an all-unsubscribed group. The per-slot
    // controls below are still the source of truth for exactly which
    // occurrences are watched; this is a bulk convenience action, not a
    // second place that mixed state needs representing.
    //
    // Deliberately left as a plain subscribe/unsubscribe checkbox rather
    // than extended to the 4-level notify cycle below: a bulk "set every
    // slot's notify level at once" control raises its own mixed-state
    // questions (what does it even mean if slots currently disagree on
    // toast/sound-enabled?) that are a separate design question from this
    // feature. Unticking it still clears each slot's toast AND sound
    // flags too (via SetCyclicSlotNotifyLevel(key, 0) below) — nothing is
    // left in an inconsistent state, this just doesn't offer a bulk way
    // to turn toast/sound on for everything at once.
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
            // Checkbox() writes the NEW post-click state into
            // allSlotsSubscribed itself before returning true here, so by
            // this point it means "user just ticked (true) / unticked
            // (false) the box" — not the pre-click value.
            //
            // Bulk-unsubscribing (just unticked) drops straight to level
            // 0 for every slot — that's what actually clears each slot's
            // toast flag too, not just a subscription toggle on its own.
            // Bulk-subscribing (just ticked) only raises a slot from 0 to
            // 1 (silent); it never demotes a slot already sitting at 2
            // (toast) back down to 1, since this box is a "make sure
            // everything's at LEAST subscribed" convenience, not a reset
            // of levels someone already configured individually.
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

    // Map-only show/hide toggle for the ENTIRE ring (background track +
    // every slot) — see CyclicGroup::shown in events.h. Same tightened-
    // checkbox treatment as DrawBasicEventRow's subscribe/shown pair, so
    // it sits at the same height as the tree arrow rather than a full
    // Checkbox frame.
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
        // Compact row: Location, Period, base Color, Custom Idle toggle,
        // and the Idle Color swatch all share one line instead of each
        // taking a full row — the previous one-field-per-line layout
        // made every group's edit panel much taller than it needed to
        // be, which matters a lot once there are a dozen-plus groups in
        // the list. Coordinates display as whole pixels ("%.0f") rather
        // than three decimal places — sub-pixel precision was never
        // meaningful here. Swatches use NoInputs so they show as a
        // small clickable square only; clicking still opens the full
        // picker (NoInputs hides the inline R/G/B/A number fields, not
        // the picker itself), so no editing power is lost.
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

        // Base color — colors.base is a plain ImVec4, so ColorEdit4 binds
        // to it directly; no read/convert/write-back round trip needed.
        ImGui::ColorEdit4("Color", &grp.colors.base.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

        // Idle color override — optional. Unchecked: idle track uses
        // colors.ter() automatically (see CyclicGroup::IdleColor() in
        // events.h); checked: the swatch becomes live and its value is
        // stored as an explicit override. Goes through ColorFloat4/
        // ColorU32 (color_utils.h) rather than ToImVec4/ShadeU32/FadeU32
        // like the base color above — idleColor is already a real ImGui
        // ImU32 (it's fed straight into DrawArc calls), not an RGBA-float
        // setting, so there's no float[4] on the other end to convert.
        ImGui::SameLine();
        bool hasCustomIdle = grp.idleColor.has_value();
        if (ImGui::Checkbox("##customcolorcyclicgroup", &hasCustomIdle))
        {
            if (hasCustomIdle)
                grp.idleColor = grp.colors.ter(); // seed with the current default, not an arbitrary color
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

        // -----------------------------------------------------------
        // Slots — the individual events within this cycle. Same
        // deferred add/remove pattern as everywhere else, nested one
        // level deeper (PushID(i) for the group above, PushID(s) for
        // each slot here, so every widget label stays unique even
        // across slots that share a name, e.g. Dry Top's two "Crash
        // Site" slots).
        // -----------------------------------------------------------
        ImGui::Spacing();
        ImGui::TextUnformatted("Events");
        ImGui::SameLine();
        bool pendingAddSlot = ImGui::SmallButton("+##add_slot");

        int pendingRemoveSlotIndex = -1;

        // editingSlotNames is declared OUTSIDE this loop (function-static
        // to DrawCyclicGroupRow), so it's shared across EVERY group this
        // function is called for, not reset per group — keying it by
        // bare slot index `s` would incorrectly conflate group A's slot 0
        // with group B's slot 0. Combining the outer group index `i`
        // with `s` gives each (group, slot) pair its own genuinely unique
        // key in the shared set.
        static std::map<int, std::string> editingSlotNames;

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            CyclicGroup::Slot& slot = grp.slots[s];
            ImGui::PushID(s);

            // Notify-level icon drawn BEFORE the name/tree-arrow, same
            // slot DrawSubscribeCheckbox used to occupy. Per SLOT (an
            // individual occurrence), not per group — this is still the
            // source of truth for exactly which occurrences are watched
            // and at what notify level. The group-level checkbox above is
            // a bulk convenience over these same per-slot subscriptions,
            // not a separate flag.
            CyclicSubscriptionKey subKey{ grp.name, slot.offset };
            int notifyLevel = GetCyclicSlotNotifyLevel(subKey);
            int newNotifyLevel = DrawNotifyLevelIcon("##notify", notifyLevel);
            if (newNotifyLevel != notifyLevel)
                SetCyclicSlotNotifyLevel(subKey, newNotifyLevel);
            ImGui::SameLine();

            // Map-only show/hide toggle for just THIS occurrence — the
            // rest of the ring (background track + other slots) still
            // draws normally. See CyclicGroup::Slot::shown in events.h.
            DrawSubscribeCheckbox("##show_slot_on_map", slot.shown);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show/hide this occurrence on the map overlay");
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            NameRowResult slotNameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, slot.name, editingSlotNames, pendingRemoveSlotIndex,
                nullptr, nullptr, [subKey]() { ToggleCyclicSlotDoneToday(subKey); },
                notifyLevel, [subKey](int lvl) { SetCyclicSlotNotifyLevel(subKey, lvl); });
            bool slotOpen = slotNameResult.open;
            if (slotNameResult.newName != slot.name)
                slot.name = slotNameResult.newName; // no RenameCategoryMember call — slots aren't categorized, only the two top-level lists are
                // Also no RenameSubscribedBasicEvent-style patch needed here:
                // cyclic slot subscriptions are keyed by (group name, slot
                // OFFSET), not slot name — see subscriptions.h — so renaming
                // a slot never invalidates its subscription key.

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

                // Repeat must evenly divide the group's period. A repeat
                // that doesn't divide evenly leaves a leftover remainder
                // so the pattern never closes cleanly back to the start
                // of the cycle; rather than silently accepting a bad
                // value, this snaps whatever the user types down to the
                // nearest actual divisor of the CURRENT period (which can
                // itself change via the Period dropdown above, so this is
                // re-validated fresh every frame, not just at entry time).
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
                    // The period itself changed since this slot's repeat
                    // was set (e.g. via the group's Period dropdown) and
                    // it no longer divides evenly — snap it down the
                    // same way, even though the user didn't touch this
                    // field directly this frame.
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

                // Custom color override — same checkbox-gates-swatch
                // pattern as the group's Custom Idle above. Seeded from
                // whatever color the slot is CURRENTLY resolving to
                // (its tier-derived shade) rather than an arbitrary
                // default, so turning the checkbox on doesn't visibly
                // change anything until the user actually picks a
                // different color.
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

                // Chat/map code for this specific slot/occurrence, same
                // as WorldEvent::chatCode. Live-edits straight into
                // slot.chatCode; not a merge key (SlotKey in
                // events_storage.cpp is name+offset only), so no
                // Save-button buffering needed here either.
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
            newSlot.duration = 600; // 10 min, a reasonable default
            newSlot.tier     = ColorTier::Primary;
            newSlot.repeat   = 1;
            grp.slots.push_back(newSlot);
        }

        ImGui::TreePop();
    }
}