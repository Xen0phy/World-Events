#pragma once
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Category
// ---------------------------------------------------------------------------
// A user-created grouping used purely to organize the options-panel list —
// it has no effect on rendering, timing, or anything outside the options
// UI. Members are referenced BY NAME, not nested/copied: a category never
// contains an actual WorldEvent or CyclicGroup, it just lists the names of
// ones that already exist in g_Events / g_CyclicGroups. This keeps
// "events.json"'s cyclicGroups/events arrays exactly as they are today —
// categories are a separate, optional layer on top, not a change to how
// the real data is shaped or stored.
//
// Name is the only identity a member is matched by (same as everywhere
// else in this codebase — see SlotKey/EventKey/GroupKey in
// events_storage.cpp). If a user renames a group/event through the
// editing UI, the rename code is responsible for patching any category
// that referenced the old name — see RenameCategoryMember() below — so a
// category never silently loses track of something just because it got
// renamed.
//
// Because members are looked up by name in g_Events/g_CyclicGroups at
// render time rather than stored as copies, a category can reference
// names from EITHER list — e.g. one category mixing a Basic Event and a
// Cyclic Event is fully supported by this data shape, even though the
// first pass of category-aware UI only builds same-list categories
// (Basic-only, Cyclic-only). Nothing here prevents mixing later.
// ---------------------------------------------------------------------------
struct Category
{
    std::string name;
    std::vector<std::string> members; // names of WorldEvent and/or CyclicGroup entries
};

// Separate category lists for Basic Events and Cyclic Events, matching the
// two top-level lists they organize. Kept as two vectors rather than one
// combined list with a "which list" tag, since every place that reads
// these already knows which UI section it's drawing.
extern std::vector<Category> g_BasicCategories;
extern std::vector<Category> g_CyclicCategories;

// ---------------------------------------------------------------------------
// RenameCategoryMember
// ---------------------------------------------------------------------------
// Call this from the SAME place a rename actually happens (i.e. right
// where ev.name = nameBuf / grp.name = nameBuf already runs in
// addon_options.cpp), passing the old and new name and which list's
// categories to patch. Finds every category containing oldName and
// updates it to newName in place. No-op (and safe) if oldName isn't in
// any category yet — most things won't be.
// ---------------------------------------------------------------------------
void RenameCategoryMember(std::vector<Category>& categories, const std::string& oldName, const std::string& newName);

// ---------------------------------------------------------------------------
// MoveCategoryMember
// ---------------------------------------------------------------------------
// Moves `memberName` into `targetCategoryIndex` (an index into
// `categories`), removing it from every OTHER category in the same list
// first — membership is exclusive, a member belongs to at most one
// category at a time, matching a folder-style metaphor rather than tags.
//
// Pass -1 for targetCategoryIndex to mean "uncategorized": the member is
// removed from every category and not added anywhere, which is exactly
// what dragging an item back out to the top-level list should do.
//
// This is a pure data operation with no UI dependency — the actual
// drag-and-drop widgets in addon_options.cpp call this once a drop is
// detected; it doesn't know or care how the move was triggered.
// ---------------------------------------------------------------------------
void MoveCategoryMember(std::vector<Category>& categories, const std::string& memberName, int targetCategoryIndex);

// ---------------------------------------------------------------------------
// SaveCategoriesData / LoadCategoriesData
// ---------------------------------------------------------------------------
// Persisted in the SAME events.json file as g_Events/g_CyclicGroups (see
// events_storage.cpp) — two extra top-level keys, "basicCategories" and
// "cyclicCategories", read/added without disturbing the rest of the file.
//
// ORDERING MATTERS: call SaveEventsData() BEFORE SaveCategoriesData() in
// the same save pass. SaveCategoriesData reads the file first (to avoid
// clobbering events/cyclicGroups when it writes back), so if it ran first
// and SaveEventsData ran after, SaveEventsData would have no knowledge of
// the category keys and would overwrite the file without them — silently
// dropping any category data just saved a moment earlier. The reverse
// order (current call site in addon.cpp) is safe: SaveCategoriesData
// always reads back whatever SaveEventsData just wrote.
//
// Both swallow exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir);
bool LoadCategoriesData(const std::string& addonDir);
