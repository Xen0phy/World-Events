// addon_options.cpp
// Implements the World Events section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns, not
// a standalone window. All widgets write directly into the global settings
// declared in settings.h / settings_table.h. There is no explicit "Save"
// button: settings are written to disk on AddonUnload (see addon.cpp), the
// same way your other addons handle it, so edits here just live in memory
// until the addon (or the game) closes.
//
// This first pass only covers the flat scalar settings (overlay visibility,
// ring radius/thickness, entry/exit window). Editing individual cyclic
// groups/slots/events is a separate, later piece once JSON persistence for
// g_CyclicGroups/g_Events exists.

#include "addon.h"
#include "settings.h"
#include "build_info.h"
#include "events.h"
#include "cyclic.h"
#include "categories.h"
#include "subscriptions.h"
#include "maprender.h"
#include "icon_whitener.h"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled / PushItemFlag — not in the public header
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>

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

// ---------------------------------------------------------------------------
// Period dropdown: whole hours only (1-6h), deliberately — no GW2 event or
// event chain runs on anything other than a whole-hour cycle, so this keeps
// the field from accepting values that imply a typo (e.g. half an hour).
// ---------------------------------------------------------------------------
static const char* const kPeriodHourLabels[] = { "1h", "2h", "3h", "4h", "5h", "6h" };
static constexpr int kPeriodHourCount = 6;

// Converts a period in seconds to a 0-based hour index for the dropdown.
// Clamped to [0, kPeriodHourCount-1] — any out-of-range or non-whole-hour
// value (which shouldn't occur from this UI, but could from a hand-edited
// JSON file) just snaps to the nearest valid hour rather than crashing or
// showing garbage.
static int PeriodSecondsToHourIndex(int periodSeconds)
{
    int hours = periodSeconds / 3600;
    if (hours < 1) hours = 1;
    if (hours > kPeriodHourCount) hours = kPeriodHourCount;
    return hours - 1;
}

// ---------------------------------------------------------------------------
// DrawBulkIconPicker
// ---------------------------------------------------------------------------
// One dropdown that sets ev.iconTexture for every event index in
// `targetIndices` at once — used by the "Basic Events" section header's
// "All icons" picker (apply to literally every event). A per-category
// version of this existed earlier but was deliberately removed, leaving
// only this section-wide bulk picker plus the individual per-event
// dropdown in DrawBasicEventRow. The function itself is still written
// generically (any index list), so it's reusable if a bulk picker is
// ever wanted somewhere else again.
//
// Display state before the user touches it: if every target already
// shares the exact same iconTexture (including "all empty", i.e. all
// using the plain dot), that shared value is shown selected. If they
// disagree, an extra "(mixed)" entry is shown selected instead — but
// "(mixed)" is purely a status display, not a real choice: it only
// appears in the list while the state is actually mixed, and selecting
// any OTHER entry applies that choice to every target and makes the
// list resolve to non-mixed on the next frame, at which point "(mixed)"
// naturally drops out of the item list entirely.
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
// HEX() in cyclic.h), which is NOT the same byte order as ImGui's own ImU32
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
// so a plain duplicate name genuinely causes merge ambiguity (the
// scenario from the Dry Top / Domain-of-Vabbi-copy-paste sessions: the
// last duplicate silently "wins" the slot in the merge, the other becomes
// an orphan). Slots, however, are matched by name+offset together
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
// level automatically. This is what gives "one list, no mixing" for free,
// matching the call made earlier this session, without the drop-target
// code needing to manually check which list a payload came from.
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
// A plain ImGui::Checkbox is noticeably taller than the TreeNode arrow it
// sits next to — Checkbox draws a full button-style frame using the
// theme's FramePadding, while TreeNode only pads its arrow by a much
// smaller amount. Sharing a row with the plain checkbox meant every row's
// height (and therefore the vertical gap between rows) was governed by
// the taller widget, which is what produced the extra vertical spacing
// once these checkboxes were added. Scoping FramePadding down to zero for
// JUST this checkbox (pushed/popped tightly around the single call, not
// left active for anything else on the row) makes the checkbox's own
// height match the tree arrow's, so the row height reverts to what it
// was before subscriptions existed.
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
// DrawDragButton
// ---------------------------------------------------------------------------
// Small button placed next to the Location field that arms/disarms
// map-drag edit mode for one Basic Event or Cyclic Group (see EditModeState
// in maprender.h). Reads "Drag" when this row isn't the one currently being
// edited, and "Stop" when it is — clicking it toggles. A hovered tooltip
// explains the interaction either way, since "Drag"/"Stop" alone doesn't
// say WHERE to actually drag it (the marker on the map, not this button).
//
// Right-click-on-the-map-marker was the original trigger for this, but
// didn't reliably reach the overlay (something upstream appears to
// intercept right-click before Nexus addons see it), so this button is the
// trigger instead — same underlying g_EditMode plumbing either way.
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
// label (so the full row stays hoverable/clickable, not just a narrow
// arrow glyph — an earlier version of this split the name out of the
// label entirely, which silently shrank the hoverable area down to just
// the arrow). Right-clicking that same TreeNode opens a small popup with
// "Edit name" and "Delete". "Edit name" doesn't replace the label — it
// just reveals an inline InputText + Save button immediately after it
// (SameLine), additively, so the always-visible name stays exactly where
// it was; the edit field is the only thing that's conditionally shown.
//
// editBuffers is a per-context std::map<int, std::string> the caller owns
// (one each for Basic Events, Cyclic Groups, Basic Categories, Cyclic
// Categories — see the static maps near each call site), keyed by index.
// The map entry IS the in-progress edit text — it's seeded once when
// editing starts and then left alone every subsequent frame (NOT
// re-synced from currentName each frame, which was a real bug: re-syncing
// every frame meant whatever the user had typed got silently overwritten
// back to the original name before Save ever saw it, so edits never
// actually stuck). InputText is allowed to mutate the map entry directly.
//
// editKey and removeIndex are deliberately separate parameters, not one
// shared index: for slots specifically, the editBuffers map is shared
// across every group (see DrawCyclicGroupRow), so the edit-tracking key
// has to combine the group index too — but pendingRemoveSlotIndex must
// still receive the bare slot index, since that's what the caller's
// grp.slots.erase(...) actually indexes by. Every OTHER call site
// (events, groups, categories) just passes the same value for both.
//
// Returns {open, newName}: open is the TreeNode's own expand/collapse
// state (callers use this exactly like the old `bool open = TreeNode(...)`
// did); newName is the possibly-edited name — callers assign this back
// into their own ev.name / grp.name / cat.name and are responsible for
// any follow-up (e.g. RenameCategoryMember). Sets pendingRemoveIndex =
// removeIndex if "Delete" was clicked.
// ---------------------------------------------------------------------------
struct NameRowResult { bool open; std::string newName; };

static NameRowResult DrawNameAndContextMenu(const char* treeNodeId, int editKey, int removeIndex, const std::string& currentName, std::map<int, std::string>& editBuffers, int& pendingRemoveIndex, const char* dragType = nullptr)
{
    bool open = ImGui::TreeNode(treeNodeId, "%s", currentName.empty() ? "(unnamed)" : currentName.c_str());

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
// a single search box, not two — per the call made this session. The
// query itself is pure transient UI state (not something worth persisting
// across sessions), hence a plain static local in AddonOptions rather
// than a settings_table.h entry.
//
// Matching is case-insensitive substring, and for Cyclic Events checks
// BOTH the group's own name AND every one of its slot names — so typing
// "Crash Site" finds Dry Top even though "Dry Top" itself doesn't contain
// that text, matching the call made this session that slot names should
// be searchable too.
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
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the World Events section inside the Nexus options panel.
// ---------------------------------------------------------------------------
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
// this frame; does not modify g_Events directly (the caller still defers
// the actual erase to after every row has been drawn, exactly as before).
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

    // Subscribe checkbox drawn BEFORE the name/tree-arrow, on the same
    // line ("[x] > Name") — see DrawSubscribeCheckbox's comment for why
    // this needs the tightened FramePadding rather than a plain
    // ImGui::Checkbox. Toggling this doesn't affect rendering or timing
    // at all (see subscriptions.h); it only adds/removes ev.name from
    // the watchlist window's list.
    bool subscribed = IsBasicEventSubscribed(ev.name);
    if (DrawSubscribeCheckbox("##subscribe", subscribed))
        ToggleBasicEventSubscription(ev.name);
    ImGui::SameLine();

    std::string oldName = ev.name;
    NameRowResult nameResult = DrawNameAndContextMenu("##event_node", i, i, ev.name, editingNames, pendingRemoveIndex, kBasicEventDragType);
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
            // ---------------------------------------------------------
            // Varying branch: a sorted list of individual start times,
            // each entered as an HH:MM time-of-day picker. Labeled UTC
            // (not auto-detected/converted — see the discussion this
            // session: the underlying schedule is UTC by design, and a
            // user in a half-hour-offset timezone mentally translating
            // their local clock when filling this in is an accepted,
            // minor inconvenience rather than something the addon
            // tries to silently correct for).
            // ---------------------------------------------------------
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
            int hourIndex = PeriodSecondsToHourIndex(ev.period);
            if (ImGui::Combo("Period", &hourIndex, kPeriodHourLabels, kPeriodHourCount))
                ev.period = (hourIndex + 1) * 3600;
        }

        // Icon dropdown — "Dot" (index 0, the default) keeps the plain
        // colored circle; any other entry names a file in the addon's
        // textures/ folder, drawn instead (tinted to the same status
        // color the dot would use — see the long authoring-requirement
        // comment on s_iconCache in maprender.cpp for why the source
        // image needs to be gray/alpha, not full color).
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
// PushID/PopID for this row are the CALLER's responsibility, same
// reasoning as DrawBasicEventRow.
//
// Sets pendingRemoveGroupIndex = i if this row's remove button was
// clicked this frame; does not modify g_CyclicGroups directly.
// ---------------------------------------------------------------------------
static void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex)
{
    static std::map<int, std::string> editingNames; // see the identical comment in DrawBasicEventRow

    CyclicGroup& grp = g_CyclicGroups[i];

    std::string oldGroupName = grp.name;
    NameRowResult nameResult = DrawNameAndContextMenu("##group_node", i, i, grp.name, editingNames, pendingRemoveGroupIndex, kCyclicGroupDragType);
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
        int hourIndex = PeriodSecondsToHourIndex(grp.period);
        if (ImGui::Combo("Period", &hourIndex, kPeriodHourLabels, kPeriodHourCount))
            grp.period = (hourIndex + 1) * 3600;

        // Base color (colors.base) — RRGGBBAA, see RGBABaseToFloat4's
        // comment above for why this needs the explicit conversion
        // rather than ColorConvertU32ToFloat4.
        ImGui::SameLine();
        ImVec4 baseColor = RGBABaseToFloat4(grp.colors.base);
        if (ImGui::ColorEdit4("Color", &baseColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
            grp.colors.base = Float4ToRGBABase(baseColor);

        // Idle color override — optional. Unchecked: idle track uses
        // colors.ter() automatically (see CyclicGroup::IdleColor() in
        // cyclic.h); checked: the swatch becomes live and its value is
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

            // Subscribe checkbox drawn BEFORE the name/tree-arrow, same
            // "[x] > Name" layout and tightened-padding reasoning as
            // DrawSubscribeCheckbox's comment. Per SLOT (an individual
            // occurrence), not per group, per the call made this
            // session: watching "Crash Site" shouldn't also silently
            // watch every other event in the same cyclic group.
            CyclicSubscriptionKey subKey{ grp.name, slot.offset };
            bool subscribed = IsCyclicSlotSubscribed(subKey);
            if (DrawSubscribeCheckbox("##subscribe", subscribed))
                ToggleCyclicSlotSubscription(subKey);
            ImGui::SameLine();

            int slotEditKey = i * 100000 + s;
            NameRowResult nameResult = DrawNameAndContextMenu("##slot_node", slotEditKey, s, slot.name, editingSlotNames, pendingRemoveSlotIndex);
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

                // Repeat must evenly divide the group's period — see
                // the long comment on CyclicGroup::Slot::repeat in
                // cyclic.h. A repeat that doesn't divide evenly leaves
                // a leftover remainder so the pattern never closes
                // cleanly back to the start of the cycle; rather than
                // silently accepting a bad value, this snaps whatever
                // the user types down to the nearest actual divisor of
                // the CURRENT period (which can itself change via the
                // Period dropdown above, so this is re-validated fresh
                // every frame, not just at entry time).
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

                // Chat/map code for this specific slot/occurrence — see
                // the identical field on WorldEvent in DrawBasicEventRow
                // for the full rationale. Live-edits straight into
                // slot.chatCode; not a merge key (see SlotKey in
                // events_storage.cpp, which is name+offset only), so no
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

void AddonOptions()
{
    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    ImGui::TextUnformatted("World Events");
    ImGui::SameLine();
    DrawIconWhitenerButton();   // opens the Icon Whitener modal when clicked
    DrawIconWhitenerPopup();    // renders the modal every frame (no-op when closed)
    ImGui::Separator();

    // Static, not a setting: pure transient UI state, not worth
    // persisting across sessions. One box filters BOTH Basic Events and
    // Cyclic Events at once — see the long comment on the search helpers
    // above for what "filters" means for each (event name only vs. group
    // name + every slot name).
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
    // just this checkbox). See subscriptions_bar.h for the full
    // rationale and ShowSubscriptionsBar's comment in settings_table.h.
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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Basic Events (g_Events) — both branches. Each event is drawn as a
    // header with its fields nested underneath, matching the indentation
    // sketch: Name / Location / Duration / Varying toggle, then either
    // Offset+Period (periodic) or a nested time-of-day list (varying).
    //
    // Add/remove are DEFERRED to after the loop, same reasoning as before —
    // and the varying branch's own per-time add/remove (one level deeper)
    // follows the identical deferred pattern, scoped to that one event's
    // varyingTimes vector.
    // -----------------------------------------------------------------------
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
    // soon (<15 min out), and waiting. These fully replace the color
    // AND alpha previously hardcoded in maprender.cpp — there's no
    // separate opacity control beyond whatever alpha the picker itself
    // lets the user choose for each color.
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

    // Size — independent of each other, NOT derived from one another the
    // way the icon size used to be hardcoded as (dot radius * 1.5). An
    // icon-using event and a plain-dot event can now look completely
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
    // offered for cyclic groups — see the comment on
    // BasicEventTimeFilterEnabled in settings_table.h for why.
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
    // category BY NAME — see categories.h — so a member name that no
    // longer corresponds to any g_Events entry (e.g. the event was
    // deleted) is simply skipped when drawing, with no special handling
    // needed; it just silently doesn't render anywhere until the category
    // itself is edited to remove that stale reference.
    //
    // Assigning an item INTO a category is drag-and-drop (a later piece —
    // see the session's planning) — this pass only covers creating,
    // renaming, and deleting categories themselves, plus drawing whatever
    // membership already exists (from a hand-edited events.json, or once
    // drag-and-drop lands). Deleting a category does NOT delete its
    // members' underlying events — members are references, not copies
    // (see categories.h) — it just dissolves the grouping, and those
    // events fall back into the uncategorized bucket on the next frame.
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
        // all, skip drawing its header entirely — previously it still
        // rendered (just force-collapsed via SetNextItemOpen(false)
        // above), which is why a non-matching category was still
        // visible, just folded shut, instead of disappearing the way a
        // non-matching event/group already does in the uncategorized
        // pass. catOpen is left false in this case (TreeNode is simply
        // never called), and the membership loop below still runs
        // unconditionally either way — see its own comment for why that
        // has to stay independent of whether the header drew at all.
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
        // regardless of catOpen — this is what's missing previously: an
        // item must stay excluded from the uncategorized pass below even
        // while its category is folded shut, since "is this item
        // categorized" and "is the category currently expanded enough to
        // draw it" are two separate questions. Only the actual row
        // DRAWING is gated on catOpen.
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
    // Cyclic Events (g_CyclicGroups) — group-level fields only for this
    // stage. Slot editing (the nested per-event list within each group) is
    // a later stage; for now each group just shows its own name, location,
    // period, base color, and an optional idle-color override, matching
    // the same deferred add/remove pattern used for Basic Events above.
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

    // Same category-aware draw order as Basic Events above — see the
    // comment there for the full reasoning (matched by name, deletions
    // dissolve the grouping without touching the underlying group,
    // membership assignment is drag-and-drop, a later piece).
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

        // Same skip-when-no-match fix as Basic Events above: when a
        // search is active and this category has no match at all, skip
        // drawing its header entirely rather than just force-collapsing
        // it — see the long comment there for the full reasoning.
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

        // Same fix as Basic Events above: membership bookkeeping runs
        // unconditionally every frame; only the row DRAWING is gated on
        // catOpen, otherwise a folded category silently leaks its
        // members back into the uncategorized list below. Search
        // filtering follows the same rule as Basic Events too: a member
        // draws if it matches OR the category's own name matches.
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