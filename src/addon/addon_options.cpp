// addon_options.cpp
// Implements the World Events section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns, not
// a standalone window. All widgets write directly into the global settings
// declared in settings.h / settings_table.h, or into g_Events/
// g_CyclicGroups/g_BasicCategories/g_CyclicCategories directly. There is no
// explicit "Save" button: everything is written to disk on AddonUnload
// (see addon.cpp), so edits here just live in memory until the addon (or
// the game) closes.
//
// Covers both the flat scalar settings (overlay visibility, ring radius/
// thickness, entry/exit window) and full editing of individual events,
// cyclic groups/slots, and categories — creating, renaming, deleting,
// recoloring, drag-and-drop categorization, and icon assignment.

#include "addon.h"
#include "settings.h"
#include "build_info.h"
#include "events.h"
#include "events_categories.h"
#include "subscriptions.h"
#include "events_tracking.h"
#include "maprender.h"
#include "icon_whitener.h"
#include "notify_sound.h"
#include "gw2_api.h"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled / PushItemFlag — not in the public header
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <functional>

// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/
// EndDisabled in this version). Usage: DisabledBlock(cond) { ...widgets... }
// While `cond` is true, widgets inside the block are non-interactive and
// drawn at half alpha. The pop happens automatically when the block ends
// (braces, return, break — anything), so there's no EndDisabled() call to
// forget.
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond) : active(cond)
    {
        if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    }
    ~ImGuiScopedDisabled()
    {
        if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
    }
    explicit operator bool() const { return true; }
};

#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

// Period field: whole hours only, 1-12h, deliberately — no GW2 event or
// event chain runs on anything other than a whole-hour cycle, so this keeps
// the field from accepting values that imply a typo (e.g. half an hour).
// Drawn as a DragInt (see DrawPeriodHoursDragInt below) rather than a Combo
// so raising the cap doesn't require growing a hardcoded label array — 12h
// covers every period seen so far (including the 7h groups) with headroom.
static constexpr int kMinPeriodHours = 1;
static constexpr int kMaxPeriodHours = 12;

// Converts a period in seconds to whole hours, clamped to
// [kMinPeriodHours, kMaxPeriodHours]. Any out-of-range or non-whole-hour
// value (which shouldn't occur from this UI, but could from a hand-edited
// JSON file) just snaps to the nearest valid hour rather than crashing or
// showing garbage.
static int PeriodSecondsToHours(int periodSeconds)
{
    int hours = periodSeconds / 3600;
    if (hours < kMinPeriodHours) hours = kMinPeriodHours;
    if (hours > kMaxPeriodHours) hours = kMaxPeriodHours;
    return hours;
}

// Draws the shared "Period" DragInt widget and writes the result (in
// seconds) back through periodSeconds if the user changed it. Factored out
// since both the Basic Event row and the Cyclic Group row need the exact
// same widget/clamp behavior — see the two call sites.
static void DrawPeriodHoursDragInt(int* periodSeconds)
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
// One dropdown that sets ev.iconTexture for every event index in
// `targetIndices` at once — used by the "Basic Events" section header's
// "All icons" picker (apply to literally every event). Written generically
// (any index list), so it's reusable for a bulk picker anywhere else too.
//
// Display state before the user touches it: if every target already
// shares the exact same iconTexture (including "all empty", i.e. all
// using the plain dot), that shared value is shown selected. If they
// disagree, a "(mixed)" entry is shown instead — purely a status display,
// not a real choice: selecting any OTHER entry applies that choice to
// every target, and "(mixed)" naturally drops out of the list once the
// state resolves to non-mixed.
// ---------------------------------------------------------------------------
static void DrawBulkIconPicker(const char* label, const std::vector<int>& targetIndices)
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
// Color conversion: ColorSet::base is RRGGBBAA (R in the top byte — see
// HEX() in events.h), which is NOT the same byte order as ImGui's own ImU32
// (ABGR, via IM_COL32). ColorConvertU32ToFloat4/ColorConvertFloat4ToU32
// assume ImGui's ABGR packing, so they can't be used directly on `base` —
// doing so would silently swap the R and B channels in the picker. These
// two helpers convert through explicit channel extraction instead,
// matching HEX()'s exact mapping, so the picker shows the true color.
//
// idleColor, by contrast, IS already a real ImGui ImU32 (it's fed directly
// into DrawArc calls in cyclicrender.cpp) — that one DOES go straight
// through ColorConvertU32ToFloat4/ColorConvertFloat4ToU32 with no special
// handling, see the idle-color swatch below.
// ---------------------------------------------------------------------------
static ImVec4 RGBABaseToFloat4(unsigned int rgba)
{
    return ImVec4(
        ((rgba >> 24) & 0xFF) / 255.0f, // R
        ((rgba >> 16) & 0xFF) / 255.0f, // G
        ((rgba >>  8) & 0xFF) / 255.0f, // B
        ( rgba        & 0xFF) / 255.0f  // A
    );
}

static unsigned int Float4ToRGBABase(const ImVec4& c)
{
    auto byteOf = [](float f) -> unsigned int {
        int v = (int)(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (unsigned int)v;
    };
    return (byteOf(c.x) << 24) | (byteOf(c.y) << 16) | (byteOf(c.z) << 8) | byteOf(c.w);
}

// ---------------------------------------------------------------------------
// Duplicate-name warnings
// ---------------------------------------------------------------------------
// These match the actual merge keys used in events_storage.cpp, not just
// "is this name used elsewhere" in general — the two are NOT the same
// thing. Groups and events are matched by name alone (GroupKey/EventKey),
// so a plain duplicate name genuinely causes merge ambiguity (e.g. from
// copy-pasting an entry as a starting point and forgetting to rename it:
// the last duplicate silently "wins" the slot in the merge, the other
// becomes an orphan). Slots, however, are matched by name+offset together
// (SlotKey) — two slots sharing a name at DIFFERENT offsets (Dry Top's
// two "Crash Site" slots) is normal and intentional, not a problem, so
// the slot-level check only flags a collision when BOTH name and offset
// match. Checking name alone for slots would incorrectly warn about
// legitimate, already-shipped data.
//
// Both take the index of the entry being checked so they can exclude it
// from the comparison — otherwise every entry would trivially "collide"
// with itself.
// ---------------------------------------------------------------------------
static bool IsDuplicateEventName(const std::vector<WorldEvent>& events, int selfIndex)
{
    const std::string& name = events[selfIndex].name;
    if (name.empty()) return false;
    for (int i = 0; i < (int)events.size(); i++)
        if (i != selfIndex && events[i].name == name)
            return true;
    return false;
}

static bool IsDuplicateGroupName(const std::vector<CyclicGroup>& groups, int selfIndex)
{
    const std::string& name = groups[selfIndex].name;
    if (name.empty()) return false;
    for (int i = 0; i < (int)groups.size(); i++)
        if (i != selfIndex && groups[i].name == name)
            return true;
    return false;
}

static bool IsDuplicateSlotKey(const std::vector<CyclicGroup::Slot>& slots, int selfIndex)
{
    const CyclicGroup::Slot& self = slots[selfIndex];
    if (self.name.empty()) return false;
    for (int i = 0; i < (int)slots.size(); i++)
        if (i != selfIndex && slots[i].name == self.name && slots[i].offset == self.offset)
            return true;
    return false;
}

static void DrawDuplicateWarning()
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

static const char* const kBasicEventDragType  = "WE_DRAG_BASIC_EVENT";
static const char* const kCyclicGroupDragType = "WE_DRAG_CYCLIC_GROUP";

// Call right after the widget that should act as the thing being dragged
// (e.g. right after drawing a row's TreeNode). itemName is whatever
// should be moved if this drag ends in a drop — i.e. the event's or
// group's current name.
static void MakeDragSource(const char* dragType, const std::string& itemName)
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
static bool MakeDropTarget(const char* dragType, std::vector<Category>& categories, int targetCategoryIndex)
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
// Small "Watch" checkbox meant to sit immediately BEFORE a TreeNode call,
// on the same line — i.e. the caller does:
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
static bool DrawSubscribeCheckbox(const char* label, bool& value)
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
// Latin plus a handful of general-purpose symbols (see
// subscriptions_window.cpp's weekly-target dot, itself just a plain
// Unicode circle); there's no bell character available without pulling
// in a whole icon font for one row-glyph.
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
static void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
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
static void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
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
static int DrawNotifyLevelIcon(const char* idSuffix, int level)
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
        default: // 3: subscribed + toast + sound — "-", the
                 // always-last icon meaning "click to fully unsubscribe"
                 // therefore last icon should always be "-"
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
static void DrawDragButton(EditTarget target, int index, const char* idSuffix)
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
struct NameRowResult { bool open; std::string newName; };

static NameRowResult DrawNameAndContextMenu(const char* treeNodeId, int editKey, int removeIndex, const std::string& currentName, std::map<int, std::string>& editBuffers, int& pendingRemoveIndex, const char* dragType = nullptr, const char* autoTag = nullptr, std::function<void()> toggleDone = nullptr,
    int notifyLevel = -1, std::function<void(int)> setNotifyLevel = nullptr)
{
    std::string label = currentName.empty() ? "(unnamed)" : currentName;
    if (autoTag)
    {
        label += " ";
        label += autoTag;
    }
    bool open = ImGui::TreeNode(treeNodeId, "%s", label.c_str());
    if (autoTag && ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatically tracked via the GW2 API.\nDrops off the Subscriptions bar/window on its own\nonce claimed today (no need to check it off by hand).");

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
// a single search box, not two. The query itself is pure transient UI
// state (not something worth persisting across sessions), hence a plain
// static local in AddonOptions rather than a settings_table.h entry.
//
// Matching is case-insensitive substring, and for Cyclic Events checks
// BOTH the group's own name AND every one of its slot names — so typing
// "Crash Site" finds Dry Top even though "Dry Top" itself doesn't contain
// that text.
// ---------------------------------------------------------------------------
static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
{
    if (needleLower.empty()) return true; // empty query matches everything
    std::string haystackLower = haystack;
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return haystackLower.find(needleLower) != std::string::npos;
}

static bool EventMatchesSearch(const WorldEvent& ev, const std::string& queryLower)
{
    return ContainsCaseInsensitive(ev.name, queryLower);
}

static bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower)
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
static void DrawBasicEventRow(int i, int& pendingRemoveIndex)
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

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        int durationMinutes = ev.duration / 60;
        if (ImGui::InputInt("Duration (min)", &durationMinutes))
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
                ImGui::SetNextItemWidth(40.0f);
                if (ImGui::InputInt("##Hour", &hour, 0, 0))
                {
                    hour = std::clamp(hour, 0, 23);
                    changed = true;
                }
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextUnformatted(":");
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::SetNextItemWidth(40.0f);
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
            // rather than conditional, since SecondsUntilNext() in
            // maprender.cpp assumes this list stays in ascending order
            // to correctly find the next upcoming time. Even a single
            // same-frame HH:MM edit can put an entry out of order
            // relative to its neighbors, not just add/remove.
            std::sort(ev.varyingTimes.begin(), ev.varyingTimes.end());
        }
        else
        {
            ImGui::SetNextItemWidth(80.0f);
            int offsetMinutes = ev.offset / 60;
            if (ImGui::InputInt("Offset (min)", &offsetMinutes))
            {
                if (offsetMinutes < 0) offsetMinutes = 0;
                ev.offset = offsetMinutes * 60;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
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

        ImGui::SetNextItemWidth(140.0f);
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

            ImGui::SetNextItemWidth(160.0f);
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
static void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex)
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
        ImGui::SetTooltip("Show this entire ring on the map overlay\n(no circle drawn at all while unchecked)");
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

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        DrawPeriodHoursDragInt(&grp.period);

        // Base color (colors.base) — RRGGBBAA, needs RGBABaseToFloat4's
        // explicit conversion rather than ColorConvertU32ToFloat4.
        ImGui::SameLine();
        ImVec4 baseColor = RGBABaseToFloat4(grp.colors.base);
        if (ImGui::ColorEdit4("Color", &baseColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            grp.colors.base = Float4ToRGBABase(baseColor);

        // Idle color override — optional. Unchecked: idle track uses
        // colors.ter() automatically (see CyclicGroup::IdleColor() in
        // events.h); checked: the swatch becomes live and its value is
        // stored as an explicit override. This DOES use
        // ColorConvertU32ToFloat4/Float4ToU32 directly, unlike the base
        // color above — idleColor is already a real ImGui ImU32 (it's
        // fed straight into DrawArc calls), not an RRGGBBAA value.
        ImGui::SameLine();
        bool hasCustomIdle = grp.idleColor.has_value();
        if (ImGui::Checkbox("Custom Idle", &hasCustomIdle))
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
            ImVec4 idleColorVec = ImGui::ColorConvertU32ToFloat4(idleU32);
            if (ImGui::ColorEdit4("Idle Color", &idleColorVec.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel) && hasCustomIdle)
                grp.idleColor = ImGui::ColorConvertFloat4ToU32(idleColorVec);
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
                ImGui::SetTooltip("Show just this occurrence on the map overlay");
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            NameRowResult nameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, slot.name, editingSlotNames, pendingRemoveSlotIndex,
                nullptr, nullptr, [subKey]() { ToggleCyclicSlotDoneToday(subKey); },
                notifyLevel, [subKey](int lvl) { SetCyclicSlotNotifyLevel(subKey, lvl); });
            bool slotOpen = nameResult.open;
            if (nameResult.newName != slot.name)
                slot.name = nameResult.newName; // no RenameCategoryMember call — slots aren't categorized, only the two top-level lists are
                // Also no RenameSubscribedBasicEvent-style patch needed here:
                // cyclic slot subscriptions are keyed by (group name, slot
                // OFFSET), not slot name — see subscriptions.h — so renaming
                // a slot never invalidates its subscription key.

            if (IsDuplicateSlotKey(grp.slots, s))
                DrawDuplicateWarning();

            if (slotOpen)
            {
                ImGui::SetNextItemWidth(80.0f);
                int offsetMinutes = slot.offset / 60;
                if (ImGui::InputInt("Offset (min)", &offsetMinutes))
                {
                    if (offsetMinutes < 0) offsetMinutes = 0;
                    slot.offset = offsetMinutes * 60;
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                int durationMinutes = slot.duration / 60;
                if (ImGui::InputInt("Duration (min)", &durationMinutes))
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
                ImGui::SetNextItemWidth(60.0f);
                int repeatInput = slot.repeat;
                if (ImGui::InputInt("Repetition", &repeatInput))
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

                ImGui::SetNextItemWidth(90.0f);
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
                if (ImGui::Checkbox("Custom Color", &hasCustomColor))
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
                    ImVec4 slotColorVec = ImGui::ColorConvertU32ToFloat4(slotU32);
                    if (ImGui::ColorEdit4("Custom Color Value", &slotColorVec.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel) && hasCustomColor)
                        slot.customColor = ImGui::ColorConvertFloat4ToU32(slotColorVec);
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

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the World Events section inside the Nexus options panel.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    OptionsRenderTimer optionsRenderTimer; // no-op unless ShowDebug is true — see ScopedRenderTimer in addon.h

    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    ImGui::SameLine();
    if constexpr (ShowDebug)
    {
        // Two separate numbers on purpose: "Render" is AddonRender alone
        // (map rings + subscriptions bar/window/notifications) — the cost
        // that actually matters during normal play. "Options UI" is this
        // panel's own per-frame rebuild cost (tree, search filter, color
        // pickers, icon previews for every group/slot) and only applies
        // while this panel is open — it was previously invisible and
        // easy to mistake for render cost when eyeballing total frame
        // time with the panel open.
        ImGui::TextDisabled("Render: %.3f ms avg (1s)", g_AvgRenderTimeMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Options UI: %.3f ms avg (1s)", g_AvgOptionsRenderTimeMs);

        // Per-view breakdown: "Data" is RefreshSubscriptionsCache plus each
        // view's own light adaptation of the shared resolved list (see
        // subscriptions_cache.h); "Draw" is everything from there to actual
        // pixels — dot/hover layout and ImGui calls for the bar, the
        // ImGui window/rows for the watchlist, popup draw/fade/expire for
        // notifications. Lets a specific "why is view X still costing Y"
        // question be answered by which of the two numbers is high,
        // instead of only having Render above as one combined total for
        // all three views together.
        ImGui::TextDisabled("Bar: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsBarDataMs, g_AvgSubsBarDrawMs);
        ImGui::SameLine();
        ImGui::TextDisabled("| Window: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsWindowDataMs, g_AvgSubsWindowDrawMs);
        ImGui::TextDisabled("Notify: %.3f data / %.3f draw ms avg (1s)", g_AvgSubsNotifyDataMs, g_AvgSubsNotifyDrawMs);
    }
    ImGui::TextUnformatted("World Events");
    ImGui::SameLine();
    DrawIconWhitenerButton();   // opens the Icon Whitener modal when clicked
    DrawIconWhitenerPopup();    // renders the modal every frame (no-op when closed)
    ImGui::Separator();

    // Static, not a setting: pure transient UI state, not worth
    // persisting across sessions. One box filters BOTH Basic Events and
    // Cyclic Events at once (event name only for Basic; group name +
    // every slot name for Cyclic).
    static char searchBuf[128] = "";
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Search##global_search", searchBuf, sizeof(searchBuf));
    std::string searchQueryLower = searchBuf;
    std::transform(searchQueryLower.begin(), searchQueryLower.end(), searchQueryLower.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    bool searchActive = !searchQueryLower.empty();

    ImGui::Checkbox("Show cyclic event overlay", &ShowCyclicOverlay);

    // Watchlist window toggle — mirrors the "Watch" checkboxes on each
    // event/slot row further down: this just opens/closes the window,
    // it doesn't affect which events are actually subscribed (that's
    // events.json data, see subscriptions.h, not this bool).
    ImGui::Checkbox("Show subscriptions window", &ShowSubscriptionsWindow);

    // Second, alternate view of the same subscription data — a thin
    // animated line pinned to the top edge of the screen (not a window:
    // no title bar, can't be dragged/resized/closed with a titlebar X,
    // just this checkbox).
    ImGui::Checkbox("Show subscriptions distribution line", &ShowSubscriptionsBar);

    // Only meaningful while the line itself is on — same dim-and-disable
    // treatment used elsewhere in this file (e.g. the watchlist controls
    // just below) so the control stays visible/discoverable rather than
    // vanishing outright.
    DisabledBlock(!ShowSubscriptionsBar)
    {
        ImGui::Indent();

        ImGui::Checkbox("Hide active on bar", &SubscriptionsBarHideActive);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Segments that are currently active are left off the bar entirely instead of showing as a dropped-to-startX line — only upcoming events are shown. Independent from \"Hide active in window\" above.");

        ImGui::Checkbox("Minimal Mode", &SubscriptionsBarMinimalMode);
        ImGui::Checkbox("Bottom Line", &SubscriptionsBarBottomAnchored);


        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Hover delay (ms)", &SubscriptionsBarHoverDelayMs, 50, 100))
        {
            // Clamp rather than reject — InputInt lets the user type/
            // arrow past either end transiently, so clamp after the
            // fact instead of blocking input. 0 is a valid, meaningful
            // value (delay disabled entirely), so the floor is 0, not 1.
            if (SubscriptionsBarHoverDelayMs < 0)    SubscriptionsBarHoverDelayMs = 0;
            if (SubscriptionsBarHoverDelayMs > 5000) SubscriptionsBarHoverDelayMs = 5000;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long the mouse has to sit still over a segment or dot before it pops out. 0 = instant.");

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Unsafe zone - left (px)", &SubscriptionsBarUnsafeLeftPx, 10, 50))
        {
            if (SubscriptionsBarUnsafeLeftPx < 0)    SubscriptionsBarUnsafeLeftPx = 0;
            if (SubscriptionsBarUnsafeLeftPx > 1000) SubscriptionsBarUnsafeLeftPx = 1000;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Width from the LEFT screen edge, in px, treated as covered by your own GW2 UI (e.g. party/buffs). Segments in this zone drop lower instead of covering it. 0 disables the left zone.");

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Unsafe zone - right (px)", &SubscriptionsBarUnsafeRightPx, 10, 50))
        {
            if (SubscriptionsBarUnsafeRightPx < 0)    SubscriptionsBarUnsafeRightPx = 0;
            if (SubscriptionsBarUnsafeRightPx > 1000) SubscriptionsBarUnsafeRightPx = 1000;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Width from the RIGHT screen edge, in px, treated as covered by your own GW2 UI (e.g. minimap/compass). Segments in this zone drop lower instead of covering it. 0 disables the right zone.");

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Unsafe zone height (px)", &SubscriptionsBarUnsafeHeightPx, 10, 50))
        {
            if (SubscriptionsBarUnsafeHeightPx < 0)    SubscriptionsBarUnsafeHeightPx = 0;
            if (SubscriptionsBarUnsafeHeightPx > 1000) SubscriptionsBarUnsafeHeightPx = 1000;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How tall your corner UI is, in px. Segments inside either unsafe zone start their drop this far down instead of from the line itself, so the popped-out block clears your UI.");

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Pop-out height (px)", &SubscriptionsBarMaxDropPx, 2, 10))
        {
            // Floored at 8, not 0 — subscriptions_bar.cpp derives the
            // detached pill's corner radius from half this value
            // (pillRx = height/2 for a true stadium cap), so it needs a
            // sane positive minimum rather than 0 being a valid "off"
            // state the way the delay/unsafe-zone settings allow.
            if (SubscriptionsBarMaxDropPx < 8)     SubscriptionsBarMaxDropPx = 8;
            if (SubscriptionsBarMaxDropPx > 300)   SubscriptionsBarMaxDropPx = 300;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How tall the dropped block/pill is, in px. Sized by default to fit two centered lines of label text; raise it if your font/DPI needs more room.");

        ImGui::Unindent();
    }

    // Not gated by ShowSubscriptionsWindow/ShowSubscriptionsBar/
    // NotificationsEnabled: this master switch drives whether ANY of the
    // three subscription views auto-surfaces this week's Wizard's Vault
    // targets on top of the user's own manual subscriptions, so it belongs
    // to "Subscriptions" as a whole rather than to any one view's controls.
    ImGui::Spacing();
    ImGui::Checkbox("Auto-track weekly Wizard's Vault targets", &WeeklyAutoTrackEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When on (default), the subscriptions window, distribution\n"
            "line, and notification popups all automatically surface any\n"
            "Basic Event / Cyclic slot that's an active-and-incomplete\n"
            "target of this week's Wizard's Vault rotation, even if you\n"
            "never subscribed to it yourself, marked with a small red\n"
            "dot/border. Turn this off to see only what you've actually\n"
            "subscribed to by hand in all three views. Doesn't affect\n"
            "the red marker on something you HAVE manually subscribed to\n"
            "that also happens to be a weekly target — that stays either way.");

    // Not gated by either ShowSubscriptionsWindow or ShowSubscriptionsBar
    // (unlike the DisabledBlock sections above/below): this key drives
    // auto-hiding an already-completed Core Boss or map meta from BOTH
    // views (subscriptions_window.cpp / subscriptions_bar.cpp), so it
    // belongs to "Subscriptions" as a whole rather than to either
    // individual view's own controls.
    ImGui::Spacing();
    ImGui::TextUnformatted("GW2 API key (Core Bosses + 8 HoT/PoF map metas)");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Needs the \"progression\" permission. When set, a subscribed\n"
            "Core Boss (Admiral Taidha Covington, Tequatl, etc.) is\n"
            "automatically left off the watchlist window and bar once\n"
            "your account has already killed it since the last daily\n"
            "reset. The same applies to any subscribed slot in Verdant\n"
            "Brink, Auric Basin, Tangled Depths, Dragon's Stand, Crystal\n"
            "Oasis, Elon Riverlands, The Desolation, or Domain of\n"
            "Vabbi, once that map's Hero's Choice Chest has already been\n"
            "claimed today (the whole ring hides together, not just the\n"
            "one slot). Nothing else is affected: the public API has no\n"
            "\"already done today\" signal for any other event type in\n"
            "this addon (other map metas, invasions, LLA, convergences),\n"
            "so those are never hidden by this.");

    {
        static char apiKeyBuf[128] = "";
        static bool bufInitialized = false;
        if (!bufInitialized) // one-time seed from the loaded setting, same pattern as other InputText fields in this file
        {
            strncpy(apiKeyBuf, Gw2ApiKey.c_str(), sizeof(apiKeyBuf) - 1);
            apiKeyBuf[sizeof(apiKeyBuf) - 1] = '\0';
            bufInitialized = true;
        }

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::InputText("##gw2_api_key", apiKeyBuf, sizeof(apiKeyBuf), ImGuiInputTextFlags_Password))
            Gw2ApiKey = apiKeyBuf;
    }

    ImGui::SameLine();
    switch (GetGw2ApiStatus())
    {
        case Gw2ApiStatus::NoKey:
            ImGui::TextDisabled("No key set");
            break;
        case Gw2ApiStatus::Pending:
            ImGui::TextDisabled("Checking...");
            break;
        case Gw2ApiStatus::Ok:
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Connected");
            break;
        case Gw2ApiStatus::InvalidKey:
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid key / missing permission");
            break;
        case Gw2ApiStatus::NetworkError:
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Network error, retrying");
            break;
    }

    // Manual counterpart to the API-based "already done today" hiding
    // above — seeevents_tracking.h. Covers everything the public API
    // doesn't (every event/slot other than the 13 Core Bosses and 8
    // HoT/PoF map chests), and works with or without an API key set.
    // Not gated by ShowSubscriptionsWindow/Bar/NotificationsEnabled for
    // the same reason WeeklyAutoTrackEnabled above isn't: right-clicking
    // to mark something done is available from all three views, so
    // clearing those marks belongs here as a shared "Subscriptions"
    // control rather than under any one view's own settings.
    ImGui::Spacing();
    ImGui::TextUnformatted("Manually-marked \"done for today\" events");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Right-click any row in the watchlist window, segment on the\n"
            "distribution line, or notification popup to mark it done for\n"
            "today. It then hides from all three views, the same way an\n"
            "API-confirmed Core Boss kill or map chest claim does, until\n"
            "the next daily reset (00:00 UTC) — or until you clear it\n"
            "below. Right-click the same row again to undo it before then.");
    if (ImGui::Button("Clear all manual done markers"))
        ClearAllDoneMarkers();

    // Everything below only makes sense while the window itself is on —
    // same dim-and-disable treatment as the cyclic overlay's controls
    // below, for the same reason (stay visible/discoverable, just
    // inert, rather than disappearing entirely).
    DisabledBlock(!ShowSubscriptionsWindow)
    {
        ImGui::Checkbox("Hide active in window", &SubscriptionsHideActive);

        ImGui::SameLine();
        ImGui::TextUnformatted("Colors:");

        // RGB only, no alpha bar — these feed straight into
        // ImGui::TextColored (plain text, no separate opacity control),
        // unlike the map's BasicEventColor* pickers just above, which
        // DO need ColorEdit4/an alpha bar since they tint an actual
        // drawn dot/icon. See SubscriptionsActiveColor's comment in
        // settings_table.h.
        ImGui::SameLine();
        ImVec4 subActiveColor = RGBABaseToFloat4(SubscriptionsActiveColor);
        if (ImGui::ColorEdit3("Active##sub_color_active", &subActiveColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            SubscriptionsActiveColor = Float4ToRGBABase(subActiveColor);

        ImGui::SameLine();
        ImVec4 subSoonColor = RGBABaseToFloat4(SubscriptionsSoonColor);
        if (ImGui::ColorEdit3("Soon##sub_color_soon", &subSoonColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            SubscriptionsSoonColor = Float4ToRGBABase(subSoonColor);
    }

    // Third, independent view of the same subscription data — small
    // lower-right toast popups (subscriptions_notification.h/.cpp) instead
    // of a persistent list/strip. Not gated by ShowSubscriptionsWindow/Bar:
    // a user may want popups without either persistent view open at all.
    ImGui::Spacing();
    ImGui::Checkbox("Enable notification popups", &NotificationsEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Pops up a small toast in the lower-right corner for anything\n"
            "on your subscription watchlist above, whether or not the\n"
            "window or distribution line are open. Click a popup to paste\n"
            "its waypoint code, same as clicking a row/segment there.");

    DisabledBlock(!NotificationsEnabled)
    {
        ImGui::Indent();

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Warn before start (min)", &NotificationLeadMinutes, 1, 5))
        {
            // 0 is a valid, meaningful value ("off" — only the on-start
            // popup below still fires), so the floor is 0, not 1.
            if (NotificationLeadMinutes < 0)   NotificationLeadMinutes = 0;
            if (NotificationLeadMinutes > 120) NotificationLeadMinutes = 120;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long before a subscribed event/slot starts to fire the \"starting soon\" popup. 0 disables it.");

        ImGui::Checkbox("Notify again when it starts", &NotificationOnStart);

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Popup duration (sec)", &NotificationDisplaySeconds, 1, 5))
        {
            if (NotificationDisplaySeconds < 1)   NotificationDisplaySeconds = 1;
            if (NotificationDisplaySeconds > 120) NotificationDisplaySeconds = 120;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long a popup stays fully visible before it fades out. Hovering a popup pauses its timer.");

        // Single notification sound file, picked from "<addon dir>/sounds".
        // Same disk-scan-and-Combo shape as the icon pickers above (see
        // DrawBulkIconPicker / GetEventIconFilenames), just for .wav files
        // instead of images — see notify_sound.h.
        //
        // This is a single global file, not a per-event choice: which
        // events actually play it is controlled by each event/slot's own
        // notify level (level 3 — see DrawNotifyLevelIcon and
        // subscriptions_notification.cpp's SpawnPopup call sites), same
        // "one global setting, per-event opt-in" split as the toast popup
        // itself. "Test" here just plays it immediately, independent of
        // any subscription state.
        {
            const std::vector<std::string>& soundFiles = GetNotificationSoundFilenames();

            // Plain label glyph — same DrawSpeakerIcon that's now level
            // 3's icon in DrawNotifyLevelIcon's cycle, reused here as a
            // visual marker for "this row is about the notification
            // sound", same fixed-square-slot sizing as DrawNotifyLevelIcon
            // uses.
            {
                float sq = ImGui::GetFrameHeight();
                ImVec2 rmin = ImGui::GetCursorScreenPos();
                ImVec2 center(rmin.x + sq * 0.5f, rmin.y + sq * 0.5f);
                DrawSpeakerIcon(ImGui::GetWindowDrawList(), center, sq * 0.96f, ImGui::GetColorU32(ImGuiCol_Text));
                ImGui::Dummy(ImVec2(sq, sq));
            }
            ImGui::SameLine();

            std::vector<const char*> soundLabels;
            soundLabels.push_back("(none)");
            for (const auto& fn : soundFiles)
                soundLabels.push_back(fn.c_str());

            int soundIndex = 0; // "(none)"
            if (!NotificationSoundFile.empty())
                for (int k = 0; k < (int)soundFiles.size(); k++)
                    if (soundFiles[k] == NotificationSoundFile) { soundIndex = k + 1; break; }

            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Sound", &soundIndex, soundLabels.data(), (int)soundLabels.size()))
                NotificationSoundFile = (soundIndex == 0) ? std::string() : soundFiles[soundIndex - 1];

            ImGui::SameLine();
            if (ImGui::Button("Rescan"))
                ScanNotificationSoundFiles();
            ImGui::SameLine();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-scans \"<addon dir>/sounds\" for .wav files you've dropped in since the dropdown was last built.");

            DisabledBlock(NotificationSoundFile.empty())
            {
                if (ImGui::Button("Test"))
                    PlayNotificationSound(NotificationSoundFile);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Drop .wav files into \"<addon dir>/sounds\" and pick one\n"
                    "here to preview it. Only .wav is supported (PlaySound has\n"
                    "no built-in decoder for mp3/ogg/etc). This doesn't play\n"
                    "automatically on a real notification yet — that's not\n"
                    "wired up in this build.");
        }

        ImGui::Unindent();
    }

    // Everything below only makes sense while the overlay itself is on —
    // dim and disable it otherwise rather than hiding it outright, so the
    // user can still see what these controls are without losing their
    // place when toggling the overlay off and back on.
    DisabledBlock(!ShowCyclicOverlay)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Ring appearance");
        ImGui::SliderFloat("Radius",    &CyclicRadius,    5.0f, 50.0f, "%.0f px");
        if ( CyclicRadius < CyclicThickness / 2 ) { CyclicThickness = CyclicRadius * 2; }
        ImGui::SliderFloat("Thickness", &CyclicThickness, 5.0f, 100.0f, "%.0f px");
        if ( CyclicThickness > CyclicRadius * 2 ) { CyclicRadius = CyclicThickness / 2; }

        ImGui::Spacing();
        ImGui::TextUnformatted("Entry / exit window");
        ImGui::TextWrapped(
            "How far ahead an upcoming event starts fading into view, and how "
            "long a finished event lingers before fading out. Measured in "
            "degrees of the ring so it scales correctly regardless of how long "
            "a given cycle runs.");
        ImGui::SliderFloat("Future window", &CyclicMaxFutureDeg, 0.0f, 360.0f, "%.0f deg");
        if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxPastDeg = 360 - CyclicMaxFutureDeg; }
        ImGui::SliderFloat("Past window",   &CyclicMaxPastDeg,   0.0f, 360.0f, "%.0f deg");
        if ( CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f ) { CyclicMaxFutureDeg = 360 - CyclicMaxPastDeg; }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputInt("Delay", &delayMilliseconds);

    // Which chat channel a watchlist row/segment/toast click pastes
    // into. Stored as the literal slash-command text itself
    // (ChatChannelPrefix, e.g. "/p ") — see settings_table.h — so this
    // Combo is the one place that owns the label<->command mapping;
    // BuildChatPasteMessage (subscriptions.cpp) just prepends whatever
    // it finds there. Index 0 ("Current chat") stores an empty prefix,
    // i.e. paste as before with no channel switch.
    {
        static const char* const kChatChannelLabels[] = {
            "Current chat (default)", "Say", "Party", "Squad",
            "Guild (represented)", "Guild 1", "Guild 2", "Guild 3",
            "Guild 4", "Guild 5", "Map"
        };
        static const char* const kChatChannelPrefixes[] = {
            "", "/s ", "/p ", "/d ",
            "/g ", "/g1 ", "/g2 ", "/g3 ",
            "/g4 ", "/g5 ", "/m "
        };
        constexpr int kChatChannelCount = sizeof(kChatChannelLabels) / sizeof(kChatChannelLabels[0]);

        int chatChannelIndex = 0;
        for (int ci = 0; ci < kChatChannelCount; ci++)
        {
            if (ChatChannelPrefix == kChatChannelPrefixes[ci]) { chatChannelIndex = ci; break; }
        }

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("Paste to", &chatChannelIndex, kChatChannelLabels, kChatChannelCount))
            ChatChannelPrefix = kChatChannelPrefixes[chatChannelIndex];

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Which chat channel a watchlist row/segment/toast click\n"
                "pastes into, regardless of whatever channel is currently\n"
                "selected in-game. Prepends that channel's slash command\n"
                "(e.g. \"/p \") before the name/waypoint. \"Current chat\"\n"
                "pastes exactly as before, into whichever channel already\n"
                "has focus.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Basic Events (g_Events) — both branches. Each event is drawn as a
    // header with its fields nested underneath, matching the indentation
    // sketch: Name / Location / Duration / Varying toggle, then either
    // Offset+Period (periodic) or a nested time-of-day list (varying).
    //
    // Add/remove are DEFERRED to after the loop — iterator/index
    // invalidation otherwise, since erasing mid-loop would shift every
    // later index. The varying branch's own per-time add/remove (one
    // level deeper) follows the identical deferred pattern, scoped to
    // that one event's varyingTimes vector.
    ImGui::TextUnformatted("Basic Events");
    MakeDropTarget(kBasicEventDragType, g_BasicCategories, -1); // drop here to uncategorize
    ImGui::SameLine();
    bool pendingAdd = ImGui::SmallButton("+##add_basic_event");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Categories");
    ImGui::SameLine();
    bool pendingAddBasicCategory = ImGui::SmallButton("+##add_basic_category");

    // Section-level bulk icon picker — applies to literally every Basic
    // Event regardless of category. There is deliberately no equivalent
    // per-category picker — only this section-wide one and the
    // individual per-event dropdown in DrawBasicEventRow exist.
    {
        std::vector<int> allIndices(g_Events.size());
        for (int i = 0; i < (int)g_Events.size(); i++) allIndices[i] = i;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextUnformatted("All icons:");
        ImGui::SameLine();
        DrawBulkIconPicker("##bulk_icon_all", allIndices);
    }

    // Status colors — one shared set for every Basic Event (not
    // per-event), matching the dot's/icon-tint's three states: active,
    // soon (<15 min out), and waiting. There's no separate opacity
    // control beyond whatever alpha the picker itself lets the user
    // choose for each color.
    {
        ImGui::TextUnformatted("Colors:");

        ImGui::SameLine();
        ImVec4 activeColor = RGBABaseToFloat4(BasicEventColorActive);
        if (ImGui::ColorEdit4("Active##basic_color_active", &activeColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            BasicEventColorActive = Float4ToRGBABase(activeColor);

        ImGui::SameLine();
        ImVec4 soonColor = RGBABaseToFloat4(BasicEventColorSoon);
        if (ImGui::ColorEdit4("Soon##basic_color_soon", &soonColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            BasicEventColorSoon = Float4ToRGBABase(soonColor);

        ImGui::SameLine();
        ImVec4 waitingColor = RGBABaseToFloat4(BasicEventColorWaiting);
        if (ImGui::ColorEdit4("Waiting##basic_color_waiting", &waitingColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            BasicEventColorWaiting = Float4ToRGBABase(waitingColor);
    }

    // Size — independent settings, not derived from one another, so an
    // icon-using event and a plain-dot event can look completely
    // different sizes relative to each other if the user wants that.
    {
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("Dot radius##basic_dot_radius", &BasicEventDotRadius, 2.0f, 30.0f, "%.0f px");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("Icon size##basic_icon_size", &BasicEventIconSize, 2.0f, 40.0f, "%.0f px");
    }

    // Zoom-based scaling — markers (and cyclic group rings, see
    // cyclicrender.cpp) grow as the map is zoomed in. Disabled by default
    // behavior is "stay fixed size", matching the old hardcoded behavior;
    // this just makes it optional and tunable.
    {
        ImGui::Checkbox("Grow markers when zooming in##basic_zoom_scaling_enabled", &BasicEventZoomScalingEnabled);

        if (BasicEventZoomScalingEnabled)
        {
            ImGui::SetNextItemWidth(80.0f);
            ImGui::SliderFloat("Start growing at##basic_zoom_start_pct", &BasicEventZoomStartPct, 0.0f, 100.0f, "%.0f%%");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::SliderFloat("Max size at 100% zoom##basic_zoom_max_mult", &BasicEventZoomMaxMultiplier, 1.0f, 4.0f, "%.1fx");
        }
    }

    // Time-window filter — only show upcoming Basic Events starting within
    // the next N minutes; active events always show. Deliberately NOT
    // offered for cyclic groups (see BasicEventTimeFilterEnabled in
    // settings_table.h).
    {
        ImGui::Checkbox("Only show events starting soon##basic_time_filter_enabled", &BasicEventTimeFilterEnabled);

        if (BasicEventTimeFilterEnabled)
        {
            // SliderInt operates on a 15-minute STEP INDEX (0..24 => 0..360
            // minutes), not minutes directly, so dragging always lands on a
            // clean 15-minute increment. The slider's printf-style format
            // is passed as a single space (a no-specifier format is valid
            // printf and just suppresses the numeric readout) since plain
            // minutes (e.g. "80") isn't the display we want past the 1h
            // mark; the h/m-formatted label is drawn separately right after
            // it instead.
            int stepIndex = BasicEventTimeFilterMinutes / 15;
            if (stepIndex < 0)  stepIndex = 0;
            if (stepIndex > 24) stepIndex = 24;

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderInt("##basic_time_filter_minutes", &stepIndex, 0, 24, " "))
                BasicEventTimeFilterMinutes = stepIndex * 15;

            int mins = BasicEventTimeFilterMinutes;
            int h    = mins / 60;
            int m    = mins % 60;

            ImGui::SameLine();
            if (h > 0 && m > 0)
                ImGui::Text("%dh %02dm", h, m);
            else if (h > 0)
                ImGui::Text("%dh", h);
            else
                ImGui::Text("%dm", m);
        }
    }

    int pendingRemoveIndex = -1;
    int pendingRemoveBasicCategoryIndex = -1;
    static std::map<int, std::string> editingBasicCategoryNames;

    // -----------------------------------------------------------------------
    // Category-aware draw order: each category's members are drawn first
    // (nested under a foldable header for that category), in the order the
    // categories themselves are listed in g_BasicCategories; whatever's
    // left over (not a member of any category) is drawn afterward as the
    // implicit "uncategorized" bucket. An item is matched into its
    // category BY NAME — see events_categories.h — so a member name that no
    // longer corresponds to any g_Events entry (e.g. the event was
    // deleted) is simply skipped when drawing, with no special handling
    // needed; it just silently doesn't render anywhere until the category
    // itself is edited to remove that stale reference.
    //
    // Assigning an item INTO a category is drag-and-drop (see
    // MakeDropTarget/BeginDragDropSource below and the payload-type
    // comment above) — this pass covers creating, renaming, and deleting
    // categories themselves, plus drawing whatever membership already
    // exists (from drag-and-drop or a hand-edited events.json). Deleting
    // a category does NOT delete its members' underlying events —
    // members are references, not copies (see events_categories.h) — it
    // just dissolves the grouping, and those events fall back into the
    // uncategorized bucket on the next frame.
    // -----------------------------------------------------------------------
    std::vector<bool> isCategorized(g_Events.size(), false);

    for (int c = 0; c < (int)g_BasicCategories.size(); c++)
    {
        Category& cat = g_BasicCategories[c];
        ImGui::PushID(1000000 + c); // offset well clear of any real event index

        // Pre-check (before drawing the TreeNode) whether this category
        // contains at least one search match, so SetNextItemOpen can
        // force it expanded BEFORE the TreeNode call itself — ImGui needs
        // to know the open state before drawing the node, not after.
        // Also pre-check whether the CATEGORY's own name matches, since a
        // category whose name itself matches should show all its
        // members, not just ones that individually match too.
        // Resolve this category's members to actual g_Events indices
        // once, up front — used both for the bulk icon picker (which
        // needs the index list before the header even draws) and reused
        // by the membership-bookkeeping loop below instead of
        // re-searching by name a second time.
        std::vector<int> memberIndices;
        for (const std::string& memberName : cat.members)
            for (int i = 0; i < (int)g_Events.size(); i++)
                if (g_Events[i].name == memberName) { memberIndices.push_back(i); break; }

        bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
        bool categoryHasMatch = categoryNameMatches;
        if (!categoryHasMatch)
            for (int i : memberIndices)
                if (EventMatchesSearch(g_Events[i], searchQueryLower))
                    categoryHasMatch = true;

        // When a search is active and this category has no match at
        // all, skip drawing its header entirely, so a non-matching
        // category disappears the same way a non-matching event/group
        // already does in the uncategorized pass, rather than just
        // sitting there folded shut. catOpen is left false in this case
        // (TreeNode is simply never called), and the membership loop
        // below still runs unconditionally regardless of whether the
        // header drew.
        bool catOpen = false;
        if (!searchActive || categoryHasMatch)
        {
            if (searchActive)
                ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

            NameRowResult nameResult = DrawNameAndContextMenu("##category_node", c, c, cat.name, editingBasicCategoryNames, pendingRemoveBasicCategoryIndex);
            catOpen = nameResult.open;
            MakeDropTarget(kBasicEventDragType, g_BasicCategories, c);
            if (nameResult.newName != cat.name)
                cat.name = nameResult.newName; // no rename-patching needed: nothing else references a CATEGORY by name (unlike events/groups, members point at THEM, not the reverse)
        }

        // Membership bookkeeping happens UNCONDITIONALLY, every frame,
        // regardless of catOpen — an item must stay excluded from the
        // uncategorized pass below even while its category is folded
        // shut, since "is this item categorized" and "is the category
        // currently expanded enough to draw it" are two separate
        // questions. Only the actual row DRAWING is gated on catOpen.
        //
        // Search filtering: a member is drawn if it matches the search
        // itself, OR if the category's own name matches (in which case
        // every member shows, not just individually-matching ones) — but
        // isCategorized[i] is set regardless of whether it's drawn, so a
        // member hidden by an active search still correctly stays out of
        // the uncategorized pass rather than incorrectly reappearing
        // there just because the search filtered it out of view here.
        for (int i : memberIndices)
        {
            isCategorized[i] = true;

            bool memberMatches = categoryNameMatches || EventMatchesSearch(g_Events[i], searchQueryLower);

            if (catOpen && memberMatches)
            {
                ImGui::PushID(i);
                DrawBasicEventRow(i, pendingRemoveIndex);
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
        WorldEvent newEvent{};
        newEvent.name       = "New Event";
        newEvent.continentX = 0.0f;
        newEvent.continentY = 0.0f;
        newEvent.isVarying  = false;
        newEvent.duration   = 900;  // 15 min, a reasonable default
        newEvent.period     = 7200; // 2h, the most common period in existing data
        newEvent.offset     = 0;
        g_Events.push_back(newEvent);
    }

    if (pendingRemoveBasicCategoryIndex >= 0)
        g_BasicCategories.erase(g_BasicCategories.begin() + pendingRemoveBasicCategoryIndex);

    if (pendingAddBasicCategory)
        g_BasicCategories.push_back({ "New Category", {} });

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Cyclic Events (g_CyclicGroups) — same deferred add/remove pattern
    // used for Basic Events above.
    // -----------------------------------------------------------------------
    ImGui::TextUnformatted("Cyclic Events");
    MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, -1); // drop here to uncategorize
    ImGui::SameLine();
    bool pendingAddGroup = ImGui::SmallButton("+##add_cyclic_group");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Categories");
    ImGui::SameLine();
    bool pendingAddCyclicCategory = ImGui::SmallButton("+##add_cyclic_category");

    int pendingRemoveGroupIndex = -1;
    int pendingRemoveCyclicCategoryIndex = -1;
    static std::map<int, std::string> editingCyclicCategoryNames;

    // Same category-aware draw order as Basic Events above.
    std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

    for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
    {
        Category& cat = g_CyclicCategories[c];
        ImGui::PushID(2000000 + c); // offset clear of both event indices and basic-category indices

        // Same pre-check as Basic Events above: figure out match state
        // BEFORE the TreeNode call, since SetNextItemOpen has to be
        // called before the node is drawn, not after.
        bool categoryNameMatches = ContainsCaseInsensitive(cat.name, searchQueryLower);
        bool categoryHasMatch = categoryNameMatches;
        if (!categoryHasMatch)
            for (const std::string& memberName : cat.members)
                for (const auto& grp : g_CyclicGroups)
                    if (grp.name == memberName && GroupMatchesSearch(grp, searchQueryLower))
                        categoryHasMatch = true;

        // When a search is active and this category has no match at all,
        // skip drawing its header entirely rather than just force-collapsing
        // it, so an empty, irrelevant category doesn't clutter search results.
        bool catOpen = false;
        if (!searchActive || categoryHasMatch)
        {
            if (searchActive)
                ImGui::SetNextItemOpen(categoryHasMatch, ImGuiCond_Always);

            NameRowResult nameResult = DrawNameAndContextMenu("##cyclic_category_node", c, c, cat.name, editingCyclicCategoryNames, pendingRemoveCyclicCategoryIndex);
            catOpen = nameResult.open;
            MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, c);
            if (nameResult.newName != cat.name)
                cat.name = nameResult.newName;
        }

        // Membership bookkeeping runs unconditionally every frame; only
        // the row DRAWING is gated on catOpen, otherwise a folded
        // category silently leaks its members back into the
        // uncategorized list below. A member draws if it matches the
        // search OR the category's own name matches.
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
        CyclicGroup newGroup{};
        newGroup.name       = "New Cycle";
        newGroup.continentX = 0.0f;
        newGroup.continentY = 0.0f;
        newGroup.period     = 7200; // 2h, the most common period in existing data
        newGroup.colors     = ColorSet{ 0x808080FF }; // neutral gray; user picks a real color next
        g_CyclicGroups.push_back(newGroup);
    }

    if (pendingRemoveCyclicCategoryIndex >= 0)
        g_CyclicCategories.erase(g_CyclicCategories.begin() + pendingRemoveCyclicCategoryIndex);

    if (pendingAddCyclicCategory)
        g_CyclicCategories.push_back({ "New Category", {} });
}