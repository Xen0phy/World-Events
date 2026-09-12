//################################################################################
// events_categories.cpp
//--------------------------------------------------------------------------------
// Storage and JSON persistence for category groupings (see events_categories.h
// for the Category/CategoryDefault types). Categories come from compiled-in
// defaults (g_Default*Categories, written by hand in
// events_basic.cpp/events_cyclic.cpp) merged with user-created ones from the
// options-panel drag-and-drop UI, keyed by id (see MergeCategoryDefaults below).
//
// Persisted in events.json alongside "events"/"cyclicGroups", as two sibling
// arrays "basicCategories"/"cyclicCategories" - the existing arrays are never
// touched; categories only reference ids that live there.
//--------------------------------------------------------------------------------

#include "events.h"   //. EVENTS_DATA_VERSION
#include "events_categories.h"
#include "localization.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<Category> g_BasicCategories;
std::vector<Category> g_CyclicCategories;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CategoryNameIdentifier / GetDefaultCategory
//--------------------------------------------------------------------------------
// Builds the event_names.csv identifier for a compiled-in category default from
// its id, scoped by list kind since basic/cyclic default ids aren't unique
// against each other (both have a "festivals"). GetDefaultCategory is the
// category equivalent of GetDefaultEvent/GetDefaultCyclicGroup (events_storage.h)
// - kept local since nothing outside this file needs it yet.
//--------------------------------------------------------------------------------
static std::string CategoryNameIdentifier(const std::string& categoryId, CategoryListKind kind)
{
    return (kind == CategoryListKind::Basic) ? "WE_NAME_CATEGORY_BASIC_" + categoryId
                                              : "WE_NAME_CATEGORY_CYCLIC_" + categoryId;
}

static const CategoryDefault* GetDefaultCategory(const std::string& id, CategoryListKind kind)
{
    const std::vector<CategoryDefault>& defaults = (kind == CategoryListKind::Basic) ? g_DefaultBasicCategories : g_DefaultCyclicCategories;
    for (const auto& def : defaults)
        if (def.id == id) return &def;
    return nullptr;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsAbandonedEntry
//--------------------------------------------------------------------------------
// True for a Category with a blank (or whitespace-only) name that also doesn't
// resolve to a compiled-in default - same "+"-created-and-never-named state as
// events_storage.cpp's IsAbandonedEntry (kept as a separate local copy, since the
// two take different default-lookup shapes). SerializeCategoryList drops these
// instead of writing them as a permanent "(unnamed)" row.
//--------------------------------------------------------------------------------
static bool IsAbandonedEntry(const std::string& customName, bool hasDefault)
{
    bool blank = customName.find_first_not_of(" \t") == std::string::npos;
    return blank && !hasDefault;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DisplayName / DisplayNameEnglish   (see: events_categories.h)
//--------------------------------------------------------------------------------
const char* DisplayName(const Category& cat, CategoryListKind kind)
{
    if (!cat.customName.empty()) return cat.customName.c_str();
    if (GetDefaultCategory(cat.id, kind)) return Tr(CategoryNameIdentifier(cat.id, kind).c_str());
    return Tr("WE_UNNAMED");
}

const char* DisplayNameEnglish(const Category& cat, CategoryListKind kind)
{
    if (!cat.customName.empty()) return cat.customName.c_str();
    if (GetDefaultCategory(cat.id, kind)) return TrEnglish(CategoryNameIdentifier(cat.id, kind).c_str());
    return TrEnglish("WE_UNNAMED");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MoveCategoryMember   (see: events_categories.h)
//--------------------------------------------------------------------------------
void MoveCategoryMember(std::vector<Category>& categories, const std::string& memberId, int targetCategoryIndex)
{
    //_ Remove from every category first so exclusivity holds; also handles targetCategoryIndex == -1 (uncategorized) for free.
    for (auto& cat : categories)
    {
        auto it = std::find(cat.members.begin(), cat.members.end(), memberId);
        if (it != cat.members.end())
            cat.members.erase(it);
    }

    if (targetCategoryIndex >= 0 && targetCategoryIndex < (int)categories.size())
        categories[targetCategoryIndex].members.push_back(memberId);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeCategory / DeserializeCategory / SerializeCategoryList / DeserializeCategoryList
//--------------------------------------------------------------------------------
// Category <-> json conversion; the List variants map the single-item versions
// over a json array (dropping abandoned unnamed entries - IsAbandonedEntry
// above). A Category always has an id by the time it reaches disk - assigned up
// front at creation (see addon_options.cpp) - so there's nothing to backfill on
// load, unlike WorldEvent/CyclicGroup/Slot (events_storage.cpp).
//--------------------------------------------------------------------------------
static json SerializeCategory(const Category& cat)
{
    json j;
    j["id"] = cat.id;
    if (!cat.customName.empty())
        j["customName"] = cat.customName;
    j["members"] = cat.members;
    return j;
}

static Category DeserializeCategory(const json& j)
{
    Category cat;
    cat.id         = j.value("id", std::string());
    cat.members    = j.value("members", std::vector<std::string>{});
    cat.customName = j.value("customName", std::string());
    return cat;
}

static json SerializeCategoryList(const std::vector<Category>& categories, CategoryListKind kind)
{
    json arr = json::array();
    for (const auto& cat : categories)
        if (!IsAbandonedEntry(cat.customName, GetDefaultCategory(cat.id, kind) != nullptr))
            arr.push_back(SerializeCategory(cat));
    return arr;
}

static std::vector<Category> DeserializeCategoryList(const json& arr)
{
    std::vector<Category> result;
    if (arr.is_array())
        for (const auto& cj : arr)
            result.push_back(DeserializeCategory(cj));
    return result;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CategoryDefaultToCategory
//--------------------------------------------------------------------------------
// Converts a compiled-in default down to a runtime Category (dropping the
// `forced` flag) so it can be used directly when no JSON version exists yet to
// win instead.
//--------------------------------------------------------------------------------
static Category CategoryDefaultToCategory(const CategoryDefault& def)
{
    Category cat;
    cat.id = def.id;
    for (const auto& m : def.members)
        cat.members.push_back(m.id);
    return cat;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeCategoryDefaults
//--------------------------------------------------------------------------------
// Same idea as MergeByKey in events_storage.cpp, keyed by category id - but a
// Category's only content beyond identity is its member list, so "JSON wins" is
// the whole merge for a matched id, not just a starting point. Every loaded
// entry already has an id (see DeserializeCategory above).
//
// Id in both: keep the JSON category as-is. Id only in defaults: always added -
// a compiled-in default can only be hidden or overridden, never truly deleted,
// so a missing id here means the default is new since the file was last saved,
// not user-removed (same reasoning as MergeByKey, events_storage.cpp). Id only
// in JSON: always kept.
//--------------------------------------------------------------------------------
static std::vector<Category> MergeCategoryDefaults(const std::vector<CategoryDefault>& defaults, const std::vector<Category>& loaded)
{
    std::unordered_map<std::string, size_t> loadedIndexById;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexById[loaded[i].id] = i;   //. last one wins on repeats

    std::vector<bool> matched(loaded.size(), false);
    std::vector<Category> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndexById.find(def.id);
        if (it != loadedIndexById.end())
        {
            result.push_back(loaded[it->second]);
            matched[it->second] = true;
        }
        else
        {
            result.push_back(CategoryDefaultToCategory(def));
        }
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ForceCategoryMembership
//--------------------------------------------------------------------------------
// Implements CategoryDefaultMember::forced (see events_categories.h):
// unconditionally places memberId into categoryId, removing it from every other
// category first. Creates categoryId if it isn't in the merged list yet
// (defensive; shouldn't normally happen - would mean a CategoryDefaultMember
// pointing at a categoryId with no matching CategoryDefault, a compile-time
// authoring mistake). Only called once the caller has confirmed the file predates
// EVENTS_DATA_VERSION.
//--------------------------------------------------------------------------------
static void ForceCategoryMembership(std::vector<Category>& categories, const std::string& categoryId, const std::string& memberId)
{
    for (auto& cat : categories)
    {
        auto it = std::find(cat.members.begin(), cat.members.end(), memberId);
        if (it != cat.members.end())
            cat.members.erase(it);
    }

    for (auto& cat : categories)
    {
        if (cat.id == categoryId)
        {
            cat.members.push_back(memberId);
            return;
        }
    }

    categories.push_back({categoryId, std::string(), {memberId}});
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveCategoriesData / LoadCategoriesData   (see: events_categories.h)
//--------------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

        //_ Read the existing file first so this only adds/updates category keys; falls back to an empty object if none exists yet.
        json j;
        {
            std::ifstream in(filepath);
            if (in.is_open())
            {
                try { j = json::parse(in); }
                catch (...) { j = json::object(); }
            }
        }

        j["basicCategories"]  = SerializeCategoryList(g_BasicCategories, CategoryListKind::Basic);
        j["cyclicCategories"] = SerializeCategoryList(g_CyclicCategories, CategoryListKind::Cyclic);

        fs::create_directories(addonDir);
        std::ofstream out(filepath);
        if (!out.is_open()) return false;
        out << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadCategoriesData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

        int64_t savedVersion = 0;
        std::vector<Category> loadedBasic;
        std::vector<Category> loadedCyclic;
        bool fileExisted = false;

        std::ifstream file(filepath);
        if (file.is_open())
        {
            fileExisted = true;
            json j = json::parse(file);

            savedVersion = j.value("data_version", (int64_t)0);

            if (j.contains("basicCategories"))
                loadedBasic = DeserializeCategoryList(j["basicCategories"]);
            if (j.contains("cyclicCategories"))
                loadedCyclic = DeserializeCategoryList(j["cyclicCategories"]);
        }

        g_BasicCategories  = MergeCategoryDefaults(g_DefaultBasicCategories,  loadedBasic);
        g_CyclicCategories = MergeCategoryDefaults(g_DefaultCyclicCategories, loadedCyclic);

        //_ Same rule as ApplyCategoryOffsetOverrides/etc (events_storage.cpp): an up-to-date file means the user's own edits win, so forced members aren't re-applied.
        if (savedVersion < EVENTS_DATA_VERSION)
        {
            for (const auto& def : g_DefaultBasicCategories)
                for (const auto& m : def.members)
                    if (m.forced)
                        ForceCategoryMembership(g_BasicCategories, def.id, m.id);

            for (const auto& def : g_DefaultCyclicCategories)
                for (const auto& m : def.members)
                    if (m.forced)
                        ForceCategoryMembership(g_CyclicCategories, def.id, m.id);
        }

        return fileExisted;
    }
    catch (...) { return false; }
}