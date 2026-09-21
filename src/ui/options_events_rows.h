//################################################################################
// options_events_rows.h
//--------------------------------------------------------------------------------
// kBasicEventDragType/kCyclicGroupDragType
//                             drag payload type strings
// MakeDropTarget              drop target that moves an item into a category
// NameEditState               what one kind of row remembers about name editing
// NameRowResult               what DrawNameAndContextMenu reports back
// DrawNameAndContextMenu      expand/collapse, name and right-click menu row
// RequestBasicEventNameEdit/RequestCyclicGroupNameEdit
//                             open a new entry's name box
// IsBasicEventCreationPending/IsCyclicGroupCreationPending
//                             a new entry is still unnamed
// RowMode                     what the caller decides about one row
// DrawBasicEventRow/DrawCyclicGroupRow
//                             full row drawers for one event or group
//--------------------------------------------------------------------------------
// The row drawers of the Events tab (a Basic Event, and a Cyclic Group with its
// slots nested) and the pieces options_events.cpp shares with them: the name row
// the category rows also use, the drag payload types and drop target, and the
// calls that open a new entry's name box. What a row draws besides that (period
// widget, notify-level controls, duplicate warning, drag button, Fix to screen
// row) is file-local in options_events_rows.cpp.
//--------------------------------------------------------------------------------

#pragma once

#include "events_categories.h" //. Category

#include <functional>
#include <map>
#include <string>
#include <vector>

//_ Payload type strings; kept distinct to prevent cross-list drops - see the .cpp.
extern const char* const kBasicEventDragType;
extern const char* const kCyclicGroupDragType;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MakeDropTarget
//--------------------------------------------------------------------------------
// Drag-and-drop: moving an item into a category. Call right after the widget that
// accepts the drop (a category header, or a section's "drop here to uncategorize"
// target). dragType picks the payload accepted (kBasicEventDragType or
// kCyclicGroupDragType); the item is moved into targetCategoryIndex by
// MoveCategoryMember() itself, and the bool return is informational.
//--------------------------------------------------------------------------------
bool MakeDropTarget(const char* dragType, std::vector<Category>& categories, int targetCategoryIndex);

//********************************************************************************
// NameEditState
//--------------------------------------------------------------------------------
// names         inline-rename text per key; an entry exists while that entry's
//               name box is open
// pendingFocus  key whose name box opens empty and focused on its first draw,
//               else -1; consumed that frame
// newKey        key of an entry added but not yet saved or cancelled, else -1;
//               the list's "+" button stays greyed until then
//--------------------------------------------------------------------------------
// What one kind of row (Basic events, Cyclic groups, slots, or one category list)
// remembers about name editing between frames. DrawNameAndContextMenu reads and
// updates it. A caller that adds an entry sets pendingFocus and newKey to the new
// entry's key, and reads newKey to grey its "+". Keys are row indices; slot keys
// are group * 100000 + slot.
//--------------------------------------------------------------------------------
struct NameEditState
{
    std::map<int, std::string> names;
    int pendingFocus = -1;
    int newKey       = -1;
};

//********************************************************************************
// NameRowResult
//--------------------------------------------------------------------------------
// open        TreeNode's current expand/collapse state
// newName     possibly-edited name; unchanged from the input until Save is
//             clicked
//--------------------------------------------------------------------------------
struct NameRowResult { bool open; std::string newName; };

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawNameAndContextMenu
//--------------------------------------------------------------------------------
// Shared expand/collapse + name + right-click "Edit name"/"Reset"/"Delete" row,
// used for Basic Events, Cyclic Groups, Cyclic slots, and both category lists.
// dragId is the MakeDragSource payload, read only when dragType is non-null - the
// two category-list callers pass neither. edit is the state of the row's kind,
// keyed by editKey. On the frame edit.pendingFocus names editKey the name box
// opens empty and focused. While edit.newKey names editKey the entry is new: the
// name box has a Cancel ("x") button that discards it outright, and newKey clears
// once the entry is saved or cancelled.
//--------------------------------------------------------------------------------
NameRowResult DrawNameAndContextMenu(
    const char*              treeNodeId,
    int                      editKey,
    int                      removeIndex,
    const std::string&       currentName,
    NameEditState&           edit,
    int&                     pendingRemoveIndex,
    const char*              dragType        = nullptr,
    const std::string&       dragId          = std::string(),
    const char*              autoTag         = nullptr,
    std::function<void()>    toggleDone      = nullptr,
    int                      notifyLevel     = -1,
    std::function<void(int)> setNotifyLevel  = nullptr,
    std::function<void()>    resetToDefault  = nullptr,
    bool                     resetAvailable  = true);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestBasicEventNameEdit / RequestCyclicGroupNameEdit
//--------------------------------------------------------------------------------
// Called once, right after pushing a freshly-created (empty-customName) entry
// onto g_Events/g_CyclicGroups, from options_events.cpp - which has no access to
// the NameEditState of DrawBasicEventRow/DrawCyclicGroupRow. Each call sets that
// state's pendingFocus and newKey. The next time that row actually draws (the
// following frame - add/remove is applied after the draw loop, same as everywhere
// else in the row drawers), DrawNameAndContextMenu opens its name box already
// focused for typing, so no placeholder name is left for the player to notice and
// replace. Cyclic Slots don't need an equivalent: their add button lives in the
// same function as their NameEditState (DrawCyclicGroupRow), so it sets the state
// inline.
//--------------------------------------------------------------------------------
void RequestBasicEventNameEdit(int index);
void RequestCyclicGroupNameEdit(int index);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventCreationPending / IsCyclicGroupCreationPending
//--------------------------------------------------------------------------------
// True from the index passed to the matching Request*NameEdit call until that
// entry is saved or cancelled - the span in which its NameEditState::newKey is
// set. The list toolbar in options_events.cpp disables the matching "+" button
// while true, capping creation to one pending, not-yet-named entry at a time.
//--------------------------------------------------------------------------------
bool IsBasicEventCreationPending();
bool IsCyclicGroupCreationPending();

//********************************************************************************
// RowMode
//--------------------------------------------------------------------------------
// deep          true when an expanded body also shows the editing fields
// linked        a deep link names this row; for a group row, the group or one of
//               its slots
// linkPending   the link has not landed yet
// linkedSlotId  group rows: id of the slot the link names, empty when it names
//               the group itself; null for a Basic row
//--------------------------------------------------------------------------------
// What the caller decides about one row. options_events.cpp builds it per row
// from the Quick/Deep toggle and its pending deep link (options_window.h), so the
// row drawers below read no window state of their own.
//--------------------------------------------------------------------------------
struct RowMode
{
    bool deep;
    bool linked;
    bool linkPending;
    const std::string* linkedSlotId;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBasicEventRow / DrawCyclicGroupRow
//--------------------------------------------------------------------------------
// Full row drawers for one g_Events[i] / g_CyclicGroups[i] entry, drawn in Quick
// and Deep mode alike: mode.deep adds the editing fields under the notify
// buttons, Done for today or Subscribe all. A group row nests its slot rows. The
// row a pending deep link names (mode.linked) opens, scrolls to the middle of the
// pane and flashes; a slot link opens its group and flashes only the slot.
// PushID/PopID around each call is the CALLER's responsibility (the same index
// can be drawn from different places depending on category membership). Neither
// modifies the underlying vector directly - each sets its pendingRemove* index
// and the caller defers the actual erase until every row for that frame has been
// drawn.
//--------------------------------------------------------------------------------
void DrawBasicEventRow(int i, const RowMode& mode, int& pendingRemoveIndex);
void DrawCyclicGroupRow(int i, const RowMode& mode, int& pendingRemoveGroupIndex);
