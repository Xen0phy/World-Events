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
// CategoryDefault / CategoryDefaultMember
// ---------------------------------------------------------------------------
// Compiled-in category defaults, defined the same way g_Events and
// g_CyclicGroups are: written by hand in events_basic.cpp/events_cyclic.cpp,
// referencing members BY NAME. This is a separate type from `Category`
// (not just a `Category` with extra data) because it needs to travel through
// LoadCategoriesData's merge step, where JSON membership normally wins —
// `forced` is the one exception to that, so it needs to survive past the
// point where everything else gets collapsed down to plain names.
//
// This is the direct replacement for grouping compiled-in entries with
// plain `//` comments (see the "Core bosses"/"LLA"/etc. comments in
// events_basic.cpp, or the expansion-name comments in events_cyclic.cpp) —
// same grouping, but now real data the options-panel UI can render as
// actual categories on a fresh install, instead of just a comment only
// visible in source.
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
// clobbering events/cyclicGroups when it writes back), so if it ran first
// and SaveEventsData ran after, SaveEventsData would have no knowledge of
// the category keys and would overwrite the file without them — silently
// dropping any category data just saved a moment earlier. The reverse
// order (current call site in addon.cpp) is safe: SaveCategoriesData
// always reads back whatever SaveEventsData just wrote.
//
// LoadCategoriesData merges g_DefaultBasicCategories/g_DefaultCyclicCategories
// (compiled-in, see above) with whatever's in the JSON, keyed by category
// name — same shape as LoadEventsData's merge in events_storage.cpp:
//   - Category name in both   -> JSON's membership wins (preserves any
//                                 drag-and-drop reorganization the user did).
//   - Name only in defaults   -> new compiled-in category, added — but
//                                 only when the saved file predates
//                                 EVENTS_DATA_VERSION; on an up-to-date
//                                 file, a missing default category means
//                                 the user deleted it, so it stays gone.
//   - Name only in the JSON   -> a user-created category, always kept.
// After that merge, any member marked `forced` (see CategoryDefaultMember
// above) is pushed into its target category — again, only when the file
// predates EVENTS_DATA_VERSION, so it seeds/re-asserts on real content
// changes without permanently overriding a user's later choice to move it.
//
// Both swallow exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir);
bool LoadCategoriesData(const std::string& addonDir);
