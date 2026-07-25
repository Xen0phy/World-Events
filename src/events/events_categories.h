//################################################################################
// events_categories.h
//--------------------------------------------------------------------------------
// Category                              named group of event/group names,
//                                        exclusive membership
// CategoryDefaultMember/CategoryDefault compiled-in category defaults, with
//                                        an optional forced flag
// RenameCategoryMember                  patches a category after a rename
// MoveCategoryMember                    moves a member between categories
// SaveCategoriesData/LoadCategoriesData persist categories to/from
//                                        events.json
//--------------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>
#include <vector>

//********************************************************************************
// Category
//--------------------------------------------------------------------------------
// name       category name, matched by exact string in JSON and by the
//            options-panel UI
// members    names of WorldEvent/CyclicGroup entries in g_Events or
//            g_CyclicGroups; membership is exclusive, at most one category
//--------------------------------------------------------------------------------
// User-created grouping for the options-panel list only; has no effect on
// rendering or timing. Members are matched by name, never nested/copied, so
// a category can reference either g_Events or g_CyclicGroups, even though
// the current UI only builds same-list categories. Renaming a group/event
// patches any referencing category via RenameCategoryMember.
//--------------------------------------------------------------------------------
struct Category
{
    std::string name;
    std::vector<std::string> members;
};

//_ Two vectors, not one combined list with a tag, since every reader
//   already knows which UI section it's drawing.
extern std::vector<Category> g_BasicCategories;
extern std::vector<Category> g_CyclicCategories;

//********************************************************************************
// CategoryDefaultMember / CategoryDefault
//--------------------------------------------------------------------------------
// CategoryDefaultMember:  name, forced (see below)
// CategoryDefault:        name, members (list of CategoryDefaultMember)
//--------------------------------------------------------------------------------
// Compiled-in defaults, written by hand in events_basic.cpp/events_cyclic.cpp
// alongside g_Events/g_CyclicGroups, referencing members by name. Kept
// separate from Category since `forced` must survive LoadCategoriesData's
// merge step, where JSON membership normally wins.
//
// forced: when true, this member is (re-)placed into this category on
// every load where the saved file predates EVENTS_DATA_VERSION, regardless
// of the user's current arrangement. Use sparingly - it overrides a user's
// own organization. Default false costs nothing: membership just seeds
// once and becomes fully user-editable after.
//
// offset: when set, one-time-pushes this member's WorldEvent::offset
// (seconds from UTC midnight) to this value, unconditionally, the same
// version-gated way as forced - see ApplyCategoryOffsetOverrides in
// events_storage.cpp.
//--------------------------------------------------------------------------------
struct CategoryDefaultMember
{
    std::string name;
    bool forced = false;   //. see block above
    std::optional<int> offset;   //. see block above
};

struct CategoryDefault
{
    std::string name;
    std::vector<CategoryDefaultMember> members;
};

//_ Consumed only by LoadCategoriesData; downstream code should use
//   g_BasicCategories/g_CyclicCategories instead.
extern std::vector<CategoryDefault> g_DefaultBasicCategories;
extern std::vector<CategoryDefault> g_DefaultCyclicCategories;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenameCategoryMember
//--------------------------------------------------------------------------------
// Call from the same place a rename happens (addon_options.cpp), passing
// the old/new name and which list to patch. Updates every category
// containing oldName; a no-op if oldName isn't in any category.
//--------------------------------------------------------------------------------
void RenameCategoryMember(std::vector<Category>& categories, const std::string& oldName, const std::string& newName);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MoveCategoryMember
//--------------------------------------------------------------------------------
// Moves memberName into targetCategoryIndex, removing it from every other
// category first - membership is exclusive. Pass -1 for "uncategorized":
// removes without adding anywhere. Pure data operation; the drag-and-drop
// UI in addon_options.cpp calls this on drop.
//--------------------------------------------------------------------------------
void MoveCategoryMember(std::vector<Category>& categories, const std::string& memberName, int targetCategoryIndex);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveCategoriesData / LoadCategoriesData
//--------------------------------------------------------------------------------
// Persisted in events.json alongside g_Events/g_CyclicGroups (see
// events_storage.cpp), as extra "basicCategories"/"cyclicCategories" keys.
//
// Call SaveEventsData() before SaveCategoriesData(): the latter reads the
// file first to avoid clobbering events/cyclicGroups, so the reverse order
// would drop the category keys.
//
// LoadCategoriesData merges the compiled-in defaults with the JSON by name
// (see MergeByKey, events_storage.cpp), then applies any forced member the
// same version-gated way. Both swallow exceptions, returning false on
// failure.
//--------------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir);
bool LoadCategoriesData(const std::string& addonDir);