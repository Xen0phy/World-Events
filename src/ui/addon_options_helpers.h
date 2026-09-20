//################################################################################
// addon_options_helpers.h
//--------------------------------------------------------------------------------
// Declarations for the World Events settings window's helper layer: scoped-
// disable, period widgets, icon/color pickers, duplicate-name checks, drag-and-
// drop plumbing, the notify-level control, the shared name/context-menu row,
// search predicates, the chat-channel combo options, and the two full row drawers
// (Basic Event / Cyclic Group).
//
// The options_*.cpp files include this and call into it directly for several
// things beyond just the row drawers (bulk icon picker, category name/context-
// menu rows, search predicates, the drag-drop "uncategorized" targets, and the
// DisabledBlock macro) - so these are declared here with external linkage instead
// of kept `static` in one .cpp.
//
// addon_options_helpers.cpp holds every implementation; nothing here should need
// editing just to change behavior, only to change a signature.
//--------------------------------------------------------------------------------

#pragma once

#include "events.h"
#include "events_categories.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "maprender.h" //. EditTarget

#include <functional>
#include <map>
#include <string>
#include <vector>

//********************************************************************************
// ImGuiScopedDisabled
//--------------------------------------------------------------------------------
// active   true while the guarded block should be non-interactive/dimmed
//--------------------------------------------------------------------------------
// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/
// EndDisabled in this version). While `active` is true, widgets drawn inside the
// scope are non-interactive and drawn at half alpha; both effects are popped
// automatically on destruction, so there's no EndDisabled() call to forget.
// Driven through the DisabledBlock macro below, not constructed directly.
//--------------------------------------------------------------------------------
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond);
    ~ImGuiScopedDisabled();
    explicit operator bool() const { return true; }
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DisabledBlock
//--------------------------------------------------------------------------------
// Usage: DisabledBlock(cond) { ...widgets... }. Wraps the block in an
// ImGuiScopedDisabled whose lifetime is the if-statement's scope, so the
// disable/dim state clears on any exit from the block (braces, return, break)
// without a matching end call. DISABLED_BLOCK_CONCAT_/CONCAT exist only to give
// each expansion a unique per-line variable name.
//--------------------------------------------------------------------------------
#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

//_ Period is whole hours only, 1-12h - see addon_options_helpers.cpp for why.
inline constexpr int kMinPeriodHours = 1;
inline constexpr int kMaxPeriodHours = 12;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PeriodSecondsToHours / DrawPeriodHoursDragInt
//--------------------------------------------------------------------------------
// Shared period widget drawn via DrawPeriodHoursDragInt by both the Basic Event
// row and the Cyclic Group row; PeriodSecondsToHours is the seconds->hours
// conversion it's built on.
//--------------------------------------------------------------------------------
int  PeriodSecondsToHours(int periodSeconds);
void DrawPeriodHoursDragInt(int* periodSeconds);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBulkIconPicker
//--------------------------------------------------------------------------------
// One dropdown that sets ev.iconTexture for every event index in `targetIndices`
// at once. Used by the "Set all icons" picker in the Basic event settings header
// (options_events.cpp), but written generically over any index list.
//--------------------------------------------------------------------------------
void DrawBulkIconPicker(const char* label, const std::vector<int>& targetIndices);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsDuplicateEventName / IsDuplicateGroupName / IsDuplicateSlotKey
//--------------------------------------------------------------------------------
// Display-only check, not a merge-key check: compares DisplayName's resolved text
// (events_storage.h), not the raw customName - see the .cpp for why. Each takes
// the index of the entry being checked so it can exclude it from the comparison;
// IsDuplicateSlotKey also takes groupId, since Slot::id (and its display name) is
// only unique within its group.
//--------------------------------------------------------------------------------
bool IsDuplicateEventName(const std::vector<WorldEvent>& events, int selfIndex);
bool IsDuplicateGroupName(const std::vector<CyclicGroup>& groups, int selfIndex);
bool IsDuplicateSlotKey(const std::vector<CyclicGroup::Slot>& slots, int selfIndex, const std::string& groupId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDuplicateWarning   (pairs with: IsDuplicateEventName/GroupName/SlotKey)
//--------------------------------------------------------------------------------
// Draws the "[duplicate]" tag next to a row whose Is*() check above came back
// true.
//--------------------------------------------------------------------------------
void DrawDuplicateWarning();

//_ Payload type strings; kept distinct to prevent cross-list drops - see the .cpp.
extern const char* const kBasicEventDragType;
extern const char* const kCyclicGroupDragType;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MakeDragSource / MakeDropTarget
//--------------------------------------------------------------------------------
// Drag-and-drop: moving an item into/out of a category. MakeDragSource goes right
// after the draggable widget; MakeDropTarget goes on whatever should accept the
// drop (a category header, or a section's "drop here to uncategorize" target).
//
// itemId is the payload (matches Category::members); displayName is only the text
// shown under the cursor while dragging.
//--------------------------------------------------------------------------------
void MakeDragSource(const char* dragType, const std::string& itemId, const std::string& displayName);
bool MakeDropTarget(const char* dragType, std::vector<Category>& categories, int targetCategoryIndex);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribeCheckbox
//--------------------------------------------------------------------------------
// Small "Watch"/"show on map" style checkbox, tightened to match a TreeNode
// arrow's height (a plain ImGui::Checkbox is noticeably taller). Meant to sit
// immediately before a TreeNode call on the same line.
//--------------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBellIcon / DrawSpeakerIcon
//--------------------------------------------------------------------------------
// Hand-drawn glyphs (no icon font in the base build) - see the .cpp for the
// geometry notes. DrawSpeakerIcon is also used standalone as a plain label glyph
// next to the notification-sound picker (options_general.cpp).
//--------------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);
void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelIcon
//--------------------------------------------------------------------------------
// 4-way notify-level control (unsubscribed / silent / toast / toast+sound). Icon
// shows the *current* level (minus/plus/bell/speaker for 0-3). Left-click always
// advances one level, wrapping 3 -> 0; jumping to an arbitrary level lives in
// DrawNameAndContextMenu's right-click menu instead (notifyLevel/ setNotifyLevel
// params below). Also used at the front of each Quick row in options_events.cpp
// for glanceable state - see DrawNotifyLevelButtons below for the direct-jump
// control in the expanded body.
//--------------------------------------------------------------------------------
int DrawNotifyLevelIcon(const char* idSuffix, int level);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNotifyLevelButtons   (pairs with: DrawNotifyLevelIcon)
//--------------------------------------------------------------------------------
// Same 0..3 notify ladder as DrawNotifyLevelIcon, laid out as four side-by-side
// hit-boxes (minus, plus, bell, speaker) instead of one cycling icon - each
// independently clickable, jumping straight to that level instead of advancing
// one step. The currently-active box is framed/highlighted. Used by the Quick
// rows in options_events.cpp, inside each row's expanded body, alongside the
// front-of-row DrawNotifyLevelIcon - the icon gives glanceable state, this gives
// a direct jump without the right-click menu DrawNotifyLevelIcon otherwise relies
// on (Quick rows have none). Returns the level to apply this frame - unchanged
// unless one of the four boxes was just clicked.
//--------------------------------------------------------------------------------
int DrawNotifyLevelButtons(const char* idSuffix, int level);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDragButton
//--------------------------------------------------------------------------------
// Arms/disarms map-drag edit mode for one Basic Event or Cyclic Group (see
// EditModeState in maprender.h).
//--------------------------------------------------------------------------------
void DrawDragButton(EditTarget target, int index, const char* idSuffix);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawFixToScreenRow
//--------------------------------------------------------------------------------
// Checkbox + (while checked) a Location-style pixel-position row for a marker/
// ring's fixedToScreen/screenX/screenY (events.h). `idSuffix` must be unique per
// caller/row - same convention as DrawDragButton above.
//--------------------------------------------------------------------------------
void DrawFixToScreenRow(const char* idSuffix, bool* fixedToScreen, float* screenX, float* screenY);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BuildChatChannelOptions
//--------------------------------------------------------------------------------
// Fills labels/prefixes with the "Paste to" combo's entries, in matching index
// order. Omits the Better Chat entry unless IsBetterChatSelfCommandEnabled()
// (better_chat.h) is true.
//--------------------------------------------------------------------------------
void BuildChatChannelOptions(std::vector<const char*>& labels, std::vector<const char*>& prefixes);

//********************************************************************************
// NameRowResult
//--------------------------------------------------------------------------------
// open        TreeNode's current expand/collapse state
// newName     possibly-edited name; unchanged from the input until Save is
//             clicked
// cancelled   true the one frame the new-entry-only Cancel button (see isNew
//             below) is clicked
//--------------------------------------------------------------------------------
struct NameRowResult { bool open; std::string newName; bool cancelled = false; };

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNameAndContextMenu
//--------------------------------------------------------------------------------
// Shared expand/collapse + name + right-click "Edit name"/"Reset"/"Delete" row,
// used for Basic Events, Cyclic Groups, Cyclic slots, and both category lists.
// dragId is the MakeDragSource payload, read only when dragType is non-null - the
// two category-list callers pass neither. autoFocus claims keyboard focus for the
// inline edit box on the single frame a freshly-created entry first draws (see
// RequestBasicEventNameEdit/RequestCyclicGroupNameEdit and the inline slot case
// in DrawCyclicGroupRow). isNew stays true every later frame until that entry is
// saved or cancelled, gating the Cancel ("x") button that discards it outright.
//--------------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(
    const char*                 treeNodeId,
    int                         editKey,
    int                         removeIndex,
    const std::string&          currentName,
    std::map<int, std::string>& editBuffers,
    int&                        pendingRemoveIndex,
    const char*                 dragType        = nullptr,
    const std::string&          dragId          = std::string(),
    const char*                 autoTag         = nullptr,
    std::function<void()>       toggleDone      = nullptr,
    int                         notifyLevel     = -1,
    std::function<void(int)>    setNotifyLevel  = nullptr,
    std::function<void()>       resetToDefault  = nullptr,
    bool                        resetAvailable  = true,
    bool                        autoFocus       = false,
    bool                        isNew           = false);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestBasicEventNameEdit / RequestCyclicGroupNameEdit
//--------------------------------------------------------------------------------
// Called once, right after pushing a freshly-created (empty-customName) entry
// onto g_Events/g_CyclicGroups, from options_events.cpp - which has no access to
// DrawBasicEventRow's/DrawCyclicGroupRow's own file-static editBuffers maps. The
// next time that row actually draws (the following frame - add/remove is applied
// after the draw loop, same as everywhere else in this file), it seeds its own
// editBuffers entry and opens already focused for typing, instead of showing a
// placeholder name the player has to notice and replace. Cyclic Slots don't need
// an equivalent: their add button lives in the same function as their editBuffers
// map (DrawCyclicGroupRow), so that seeding happens inline instead - see the
// .cpp.
//--------------------------------------------------------------------------------
void RequestBasicEventNameEdit(int index);
void RequestCyclicGroupNameEdit(int index);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventCreationPending / IsCyclicGroupCreationPending
//--------------------------------------------------------------------------------
// True from the index passed to the matching Request*NameEdit call until that
// entry is saved or cancelled (NameRowResult::cancelled) - the same span
// DrawBasicEventRow/DrawCyclicGroupRow pass through as isNew. The list toolbar in
// options_events.cpp disables the matching "+" button while true, capping
// creation to one pending, not-yet-named entry at a time.
//--------------------------------------------------------------------------------
bool IsBasicEventCreationPending();
bool IsCyclicGroupCreationPending();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContainsCaseInsensitive / EventMatchesSearch / GroupMatchesSearch
//--------------------------------------------------------------------------------
// Search: one shared query filters both Basic Events and Cyclic Events. Cyclic
// matching checks both the group's own name and every slot name.
//--------------------------------------------------------------------------------
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower);
bool EventMatchesSearch(const WorldEvent& ev, const std::string& queryLower);
bool GroupMatchesSearch(const CyclicGroup& grp, const std::string& queryLower);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicEventRow / DrawCyclicGroupRow
//--------------------------------------------------------------------------------
// Full row drawers for one g_Events[i] / g_CyclicGroups[i] entry. PushID/PopID
// around each call is the CALLER's responsibility (the same index can be drawn
// from different places depending on category membership). Neither modifies the
// underlying vector directly - each sets its pendingRemove* index and the caller
// defers the actual erase until every row for that frame has been drawn.
//--------------------------------------------------------------------------------
void DrawBasicEventRow(int i, int& pendingRemoveIndex);
void DrawCyclicGroupRow(int i, int& pendingRemoveGroupIndex);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tooltip
//--------------------------------------------------------------------------------
// Displays a plain-text tooltip for the last ImGui item, but only after the
// cursor has been hovering for delaySeconds (default 0.5s), to keep tooltips from
// flickering up while the player is just moving the mouse across the window.
// Reads GImGui->HoveredIdTimer from imgui_internal.h (an ImGui internal, but the
// standard approach for hover-delay tooltips prior to ImGui's own hover-delay API
// landing in later versions).
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ItemPreviewGate
//--------------------------------------------------------------------------------
// Shorthand for "the widget just drawn should currently be showing its live
// preview" - true while it's either being dragged/typed into (IsItemActive) or
// just moused over (IsItemHovered). Both the subscriptions bar's unsafe-zone
// preview lines and the toast layout preview (RequestNotificationLayoutPreview,
// subscriptions_ui.h) gate their preview on exactly this, so callers share one
// spot instead of repeating the OR. Call right after the widget it applies to.
//--------------------------------------------------------------------------------
inline bool ItemPreviewGate()
{
    return ImGui::IsItemActive() || ImGui::IsItemHovered();
}