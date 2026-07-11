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
// ones that already exist in g_Events / g_CyclicGroups. Categories are a
// separate, optional layer on top of events.json's existing cyclicGroups/
// events arrays, not a change to how that data is shaped or stored.
//
// Name is the only identity a member is matched by (same as everywhere
// else in this codebase — see SlotKey/EventKey/GroupKey in
// events_storage.cpp). If a user renames a group/event through the
// editing UI, the rename code patches any category that referenced the
// old name — see RenameCategoryMember() below.
//
// Because members are looked up by name in g_Events/g_CyclicGroups at
// render time rather than stored as copies, a category can reference
// names from EITHER list — e.g. one category mixing a Basic Event and a
// Cyclic Event works fine with this data shape, even though the current
// category-aware UI only builds same-list categories (Basic-only,
// Cyclic-only).
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
// CategoryDefault / CategoryDefaultMember
// ---------------------------------------------------------------------------
// Compiled-in category defaults, defined the same way g_Events and
// g_CyclicGroups are: written by hand in events_basic.cpp/events_cyclic.cpp,
// referencing members BY NAME. This is a separate type from `Category`
// because it needs to travel through LoadCategoriesData's merge step,
// where JSON membership normally wins — `forced` is the one exception to
// that, so it needs to survive past the point where everything else gets
// collapsed down to plain names.
struct CategoryDefaultMember
{
    std::string name;

    // When true, this member is pushed into this category on every load
    // where the saved file predates EVENTS_DATA_VERSION (see events.h),
    // REGARDLESS of where the user's file currently has it — even if
    // they'd previously dragged it to a different category, or out to
    // uncategorized. Use sparingly: this overrides a user's own
    // organization choice, so it's meant for "this one really belongs
    // here" cases, not routine grouping. Default false means adding a
    // new compiled-in category costs nothing extra for the common case —
    // membership just seeds once and is then fully user-editable.
    bool forced = false;
};

struct CategoryDefault
{
    std::string name;
    std::vector<CategoryDefaultMember> members;
};

// Defined in events_basic.cpp and events_cyclic.cpp, right alongside
// g_Events/g_CyclicGroups, mirroring that split. Consumed only by
// LoadCategoriesData (events_categories.cpp) — nothing else should read these
// directly, since g_BasicCategories/g_CyclicCategories are what everything
// downstream (the options panel, rendering) actually uses.
extern std::vector<CategoryDefault> g_DefaultBasicCategories;
extern std::vector<CategoryDefault> g_DefaultCyclicCategories;

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
// clobbering events/cyclicGroups when it writes back), so if it ran
// first, SaveEventsData would overwrite the file without the category
// keys. The reverse order (current call site in addon.cpp) is safe.
//
// LoadCategoriesData merges g_DefaultBasicCategories/g_DefaultCyclicCategories
// with whatever's in the JSON, keyed by category name — same merge rule as
// LoadEventsData (see events_storage.cpp's MergeByKey comment). After that
// merge, any member marked `forced` (see CategoryDefaultMember above) is
// pushed into its target category, gated the same way as the merge itself
// (see EVENTS_DATA_VERSION in events.h) so it re-asserts on real content
// changes without overriding a user's later choice to move it.
//
// Both swallow exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir);
bool LoadCategoriesData(const std::string& addonDir);
