// addon_options_helpers.h
// Declarations for the World Events options panel's helper layer:
// scoped-disable, period widgets, icon/color pickers, duplicate-name
// checks, drag-and-drop plumbing, the notify-level control, the shared
// name/context-menu row, search predicates, and the two full row
// drawers (Basic Event / Cyclic Group).
//
// addon_options.cpp (AddonOptions() itself) includes this and calls into
// it directly for several things beyond just the row drawers (bulk icon
// picker, color conversion, category name/context-menu rows, search
// predicates, the drag-drop "uncategorized" targets, and the
// DisabledBlock macro) — so these are declared here with external
// linkage rather than kept `static` in one .cpp.
//
// addon_options_helpers.cpp holds every implementation; nothing here
// should need editing just to change behavior, only to change a
// signature.

#pragma once

#include "events.h"
#include "events_categories.h"
#include "maprender.h" // EditTarget
#include "imgui.h"
#include "imgui_internal.h"

#include <vector>
#include <string>
#include <map>
#include <functional>

// ---------------------------------------------------------------------------
// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/
// EndDisabled in this version). Usage: DisabledBlock(cond) { ...widgets... }
// While `cond` is true, widgets inside the block are non-interactive and
// drawn at half alpha. The pop happens automatically when the block ends
// (braces, return, break — anything), so there's no EndDisabled() call to
// forget.
// ---------------------------------------------------------------------------
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond);
    ~ImGuiScopedDisabled();
    explicit operator bool() const { return true; }
};

#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

// ---------------------------------------------------------------------------
// Period field: whole hours only, 1-12h — see addon_options_helpers.cpp
// for why. Drawn via DrawPeriodHoursDragInt by both the Basic Event row
// and the Cyclic Group row.
// ---------------------------------------------------------------------------
inline constexpr int kMinPeriodHours = 1;
inline constexpr int kMaxPeriodHours = 12;

int  PeriodSecondsToHours(int periodSeconds);
void DrawPeriodHoursDragInt(int* periodSeconds);

// ---------------------------------------------------------------------------
// One dropdown that sets ev.iconTexture for every event index in
// `targetIndices` at once. Used by the Basic Events section header's
// "All icons" picker, but written generically over any index list.
// ---------------------------------------------------------------------------
void DrawBulkIconPicker(const char* label, const std::vector<int>& targetIndices);

// ---------------------------------------------------------------------------
// Color conversion: ColorSet::base is RRGGBBAA (see HEX() in events.h),
// which is NOT ImGui's own ABGR packing, so it can't go through
// ColorConvertU32ToFloat4/Float4ToU32 directly — these two do the
// explicit channel mapping instead. idleColor, notably, IS a real ImGui
// ImU32 already and does NOT use these; see cyclicrender.cpp / the group
// row's idle-color swatch.
// ---------------------------------------------------------------------------
ImVec4       RGBABaseToFloat4(unsigned int rgba);
unsigned int Float4ToRGBABase(const ImVec4& c);

// ---------------------------------------------------------------------------
// Duplicate-name warnings. These match the actual merge keys used in
// events_storage.cpp: events/groups are matched by name alone, slots by
// name+offset together (two slots can legitimately share a name at
// different offsets — see the .cpp for the full explanation).
// ---------------------------------------------------------------------------
bool IsDuplicateEventName(const std::vector<WorldEvent>& events, int selfIndex);
bool IsDuplicateGroupName(const std::vector<CyclicGroup>& groups, int selfIndex);
bool IsDuplicateSlotKey(const std::vector<CyclicGroup::Slot>& slots, int selfIndex);
void DrawDuplicateWarning();

// ---------------------------------------------------------------------------
// Drag-and-drop: moving an item into/out of a category. Two distinct
// payload TYPE STRINGS give "one list, no mixing" for free — see the
// .cpp for why. MakeDragSource goes right after the draggable widget;
// MakeDropTarget goes on whatever should accept the drop (a category
// header, or a section's "drop here to uncategorize" target).
// ---------------------------------------------------------------------------
extern const char* const kBasicEventDragType;
extern const char* const kCyclicGroupDragType;

void MakeDragSource(const char* dragType, const std::string& itemName);
bool MakeDropTarget(const char* dragType, std::vector<Category>& categories, int targetCategoryIndex);

// ---------------------------------------------------------------------------
// Small "Watch"/"show on map" style checkbox, tightened to match a
// TreeNode arrow's height (a plain ImGui::Checkbox is noticeably taller).
// Meant to sit immediately before a TreeNode call on the same line.
// ---------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value);

// ---------------------------------------------------------------------------
// Hand-drawn glyphs (no icon font in the base build) — see the .cpp for
// the geometry notes. DrawSpeakerIcon is also used standalone as a plain
// label glyph next to the notification-sound picker in AddonOptions.
// ---------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);
void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);

// ---------------------------------------------------------------------------
// 4-way notify-level control (unsubscribed / silent / toast / toast+sound).
// Left-click always advances one level, wrapping 3 -> 0; jumping to an
// arbitrary level lives in DrawNameAndContextMenu's right-click menu
// instead (notifyLevel/setNotifyLevel params below).
// ---------------------------------------------------------------------------
int DrawNotifyLevelIcon(const char* idSuffix, int level);

// ---------------------------------------------------------------------------
// Arms/disarms map-drag edit mode for one Basic Event or Cyclic Group
// (see EditModeState in maprender.h).
// ---------------------------------------------------------------------------
void DrawDragButton(EditTarget target, int index, const char* idSuffix);

// ---------------------------------------------------------------------------
// Shared expand/collapse + name + right-click "Edit name"/"Delete" row,
// used for Basic Events, Cyclic Groups, Cyclic slots, and both category
// lists. See the .cpp for the full contract on editBuffers/editKey/
// removeIndex, autoTag, toggleDone, and notifyLevel/setNotifyLevel.
// ---------------------------------------------------------------------------
struct NameRowResult { bool open; std::string newName; };

NameRowResult DrawNameAndContextMenu(
    const char* treeNodeId,
    int editKey,
    int removeIndex,
    const std::string& currentName,
    std::map<int, std::string>& editBuffers,
    int& pendingRemoveIndex,
    const char* dragType = nullptr,
    const char* autoTag = nullptr,
    std::function<void()> toggleDone = nullptr,
    int notifyLevel = -1,
    std::function<void(int)> setNotifyLevel = nullptr);

// ---------------------------------------------------------------------------
// Search: one shared query filters both Basic Events and Cyclic Events.
// Cyclic matching checks both the group's own name and every slot name.
// ---------------------------------------------------------------------------
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower);
bool EventMatchesSearch(const WorldEvent& ev, const std::string& queryLower);
bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower);

// ---------------------------------------------------------------------------
// Full row drawers for one g_Events[i] / g_CyclicGroups[i] entry.
// PushID/PopID around each call is the CALLER's responsibility (the same
// index can be drawn from different places depending on category
// membership). Neither modifies the underlying vector directly — each
// sets its pendingRemove* index and the caller defers the actual erase
// until every row for that frame has been drawn.
// ---------------------------------------------------------------------------
void DrawBasicEventRow(int i, int& pendingRemoveIndex);
void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex);

// ---------------------------------------------------------------------------
// Tooltip (inline helper)
// ---------------------------------------------------------------------------
// Displays a plain-text tooltip for the last ImGui item, but only after the
// cursor has been hovering for delaySeconds (default 0.5 s). The delay
// prevents tooltips from flickering up while the player is just moving the
// mouse across the window.
//
// Uses GImGui->HoveredIdTimer from imgui_internal.h to read how long the
// current item has been hovered — this is an ImGui internal, but it's the
// standard approach for hover-delay tooltips prior to ImGui's built-in
// SetNextWindowContentSize hover-delay API landing in later versions.
// ---------------------------------------------------------------------------
inline void Tooltip(const char* text, float delaySeconds = 0.5f)
{
    if (ImGui::IsItemHovered())
    {
        if (GImGui->HoveredIdTimer >= delaySeconds)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", text);
            ImGui::EndTooltip();
        }
    }
}