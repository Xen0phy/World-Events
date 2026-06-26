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

#include "settings.h"
#include "build_info.h"
#include "events.h"
#include "cyclic.h"
#include "categories.h"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled / PushItemFlag — not in the public header
#include <cstring>
#include <algorithm>

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
    WorldEvent& ev = g_Events[i];

    // Name buffer is separate from ev.name (a std::string) because
    // InputText needs a raw, fixed-size char* to write into — it can't
    // write directly into a std::string in this ImGui version. Synced
    // from ev.name once per row per frame; written back immediately
    // whenever the user changes it (InputText's own return value tells
    // us a change happened this frame, so there's no separate "did the
    // user finish typing" step needed).
    char nameBuf[128];
    strncpy(nameBuf, ev.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';

    bool open = ImGui::TreeNode("##event_node", "%s", ev.name.empty() ? "(unnamed)" : ev.name.c_str());
    MakeDragSource(kBasicEventDragType, ev.name);
    ImGui::SameLine();
    bool removeClicked = ImGui::SmallButton("-##remove");
    if (removeClicked)
        pendingRemoveIndex = i;
    if (IsDuplicateEventName(g_Events, i))
        DrawDuplicateWarning();

    if (open)
    {
        std::string oldName = ev.name;
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            ev.name = nameBuf;
            RenameCategoryMember(g_BasicCategories, oldName, ev.name);
        }

        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputFloat2("Location", &ev.continentX, "%.0f");

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
    CyclicGroup& grp = g_CyclicGroups[i];

    char nameBuf[128];
    strncpy(nameBuf, grp.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';

    bool open = ImGui::TreeNode("##group_node", "%s", grp.name.empty() ? "(unnamed)" : grp.name.c_str());
    MakeDragSource(kCyclicGroupDragType, grp.name);
    ImGui::SameLine();
    if (ImGui::SmallButton("-##remove_group"))
        pendingRemoveGroupIndex = i;
    if (IsDuplicateGroupName(g_CyclicGroups, i))
        DrawDuplicateWarning();

    if (open)
    {
        std::string oldGroupName = grp.name;
        if (ImGui::InputText("Cycle Name", nameBuf, sizeof(nameBuf)))
        {
            grp.name = nameBuf;
            RenameCategoryMember(g_CyclicCategories, oldGroupName, grp.name);
        }

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
        ImGui::SetNextItemWidth(70.0f);
        int hourIndex = PeriodSecondsToHourIndex(grp.period);
        if (ImGui::Combo("Period", &hourIndex, kPeriodHourLabels, kPeriodHourCount))
            grp.period = (hourIndex + 1) * 3600;

        // Base color (colors.base) — RRGGBBAA, see RGBABaseToFloat4's
        // comment above for why this needs the explicit conversion
        // rather than ColorConvertU32ToFloat4.
        ImGui::SameLine();
        ImVec4 baseColor = RGBABaseToFloat4(grp.colors.base);
        if (ImGui::ColorEdit4("Color", &baseColor.x, ImGuiColorEditFlags_NoInputs))
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
            if (ImGui::ColorEdit4("Idle Color", &idleColorVec.x, ImGuiColorEditFlags_NoInputs) && hasCustomIdle)
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

        for (int s = 0; s < (int)grp.slots.size(); s++)
        {
            CyclicGroup::Slot& slot = grp.slots[s];
            ImGui::PushID(s);

            char slotNameBuf[128];
            strncpy(slotNameBuf, slot.name.c_str(), sizeof(slotNameBuf) - 1);
            slotNameBuf[sizeof(slotNameBuf) - 1] = '\0';

            bool slotOpen = ImGui::TreeNode("##slot_node", "%s", slot.name.empty() ? "(unnamed)" : slot.name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("-##remove_slot"))
                pendingRemoveSlotIndex = s;
            if (IsDuplicateSlotKey(grp.slots, s))
                DrawDuplicateWarning();

            if (slotOpen)
            {
                if (ImGui::InputText("Event Name", slotNameBuf, sizeof(slotNameBuf)))
                    slot.name = slotNameBuf;

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
                    if (ImGui::ColorEdit4("Custom Color Value", &slotColorVec.x, ImGuiColorEditFlags_NoInputs) && hasCustomColor)
                        slot.customColor = ImGui::ColorConvertFloat4ToU32(slotColorVec);
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
    ImGui::Separator();

    ImGui::Checkbox("Show cyclic event overlay", &ShowCyclicOverlay);

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

    int pendingRemoveIndex = -1;
    int pendingRemoveBasicCategoryIndex = -1;

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

        char catNameBuf[128];
        strncpy(catNameBuf, cat.name.c_str(), sizeof(catNameBuf) - 1);
        catNameBuf[sizeof(catNameBuf) - 1] = '\0';

        bool catOpen = ImGui::TreeNode("##category_node", "%s", cat.name.empty() ? "(unnamed category)" : cat.name.c_str());
        MakeDropTarget(kBasicEventDragType, g_BasicCategories, c);
        ImGui::SameLine();
        if (ImGui::SmallButton("-##remove_category"))
            pendingRemoveBasicCategoryIndex = c;

        if (catOpen)
        {
            if (ImGui::InputText("Category Name", catNameBuf, sizeof(catNameBuf)))
                cat.name = catNameBuf; // no rename-patching needed: nothing else references a CATEGORY by name (unlike events/groups, members point at THEM, not the reverse)
        }

        // Membership bookkeeping happens UNCONDITIONALLY, every frame,
        // regardless of catOpen — this is what's missing previously: an
        // item must stay excluded from the uncategorized pass below even
        // while its category is folded shut, since "is this item
        // categorized" and "is the category currently expanded enough to
        // draw it" are two separate questions. Only the actual row
        // DRAWING is gated on catOpen.
        for (const std::string& memberName : cat.members)
        {
            for (int i = 0; i < (int)g_Events.size(); i++)
            {
                if (g_Events[i].name != memberName) continue;
                isCategorized[i] = true;

                if (catOpen)
                {
                    ImGui::PushID(i);
                    DrawBasicEventRow(i, pendingRemoveIndex);
                    ImGui::PopID();
                }
                break; // names are this codebase's identity key — see
                       // categories.h — so the first match is the only
                       // one that should exist; stop once found rather
                       // than potentially drawing a same-named event
                       // twice if a duplicate exists (see the
                       // duplicate-name warning elsewhere in this file
                       // for surfacing that underlying problem itself).
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

    // Same category-aware draw order as Basic Events above — see the
    // comment there for the full reasoning (matched by name, deletions
    // dissolve the grouping without touching the underlying group,
    // membership assignment is drag-and-drop, a later piece).
    std::vector<bool> isGroupCategorized(g_CyclicGroups.size(), false);

    for (int c = 0; c < (int)g_CyclicCategories.size(); c++)
    {
        Category& cat = g_CyclicCategories[c];
        ImGui::PushID(2000000 + c); // offset clear of both event indices and basic-category indices

        char catNameBuf[128];
        strncpy(catNameBuf, cat.name.c_str(), sizeof(catNameBuf) - 1);
        catNameBuf[sizeof(catNameBuf) - 1] = '\0';

        bool catOpen = ImGui::TreeNode("##cyclic_category_node", "%s", cat.name.empty() ? "(unnamed category)" : cat.name.c_str());
        MakeDropTarget(kCyclicGroupDragType, g_CyclicCategories, c);
        ImGui::SameLine();
        if (ImGui::SmallButton("-##remove_cyclic_category"))
            pendingRemoveCyclicCategoryIndex = c;

        if (catOpen)
        {
            if (ImGui::InputText("Category Name", catNameBuf, sizeof(catNameBuf)))
                cat.name = catNameBuf;
        }

        // Same fix as Basic Events above: membership bookkeeping runs
        // unconditionally every frame; only the row DRAWING is gated on
        // catOpen, otherwise a folded category silently leaks its
        // members back into the uncategorized list below.
        for (const std::string& memberName : cat.members)
        {
            for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
            {
                if (g_CyclicGroups[i].name != memberName) continue;
                isGroupCategorized[i] = true;

                if (catOpen)
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
