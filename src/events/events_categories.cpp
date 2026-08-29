//################################################################################
// events_categories.cpp
//--------------------------------------------------------------------------------
// Storage and JSON persistence for category groupings (see events_categories.h
// for the Category/CategoryDefault types). Categories come from compiled-in
// defaults (g_Default*Categories, written by hand in
// events_basic.cpp/events_cyclic.cpp) merged with user-created ones from the
// options-panel drag-and-drop UI, keyed by name (see MergeCategoryDefaults
// below).
//
// Persisted in events.json alongside "events"/"cyclicGroups", as two sibling
// arrays "basicCategories"/"cyclicCategories" - the existing arrays are never
// touched; categories only reference names that live there.
//--------------------------------------------------------------------------------

#include "events.h"   //. EVENTS_DATA_VERSION
#include "events_categories.h"
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
// RenameCategoryMember
//--------------------------------------------------------------------------------
void RenameCategoryMember(std::vector<Category>& categories, const std::string& oldName, const std::string& newName)
{
    if (oldName == newName) return;

    //_ No break - every occurrence gets patched if oldName appears in multiple/repeated categories, not just the first.
    for (auto& cat : categories)
        for (auto& member : cat.members)
            if (member == oldName)
                member = newName;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MoveCategoryMember
//--------------------------------------------------------------------------------
void MoveCategoryMember(std::vector<Category>& categories, const std::string& memberName, int targetCategoryIndex)
{
    //_ Remove from every category first so exclusivity holds; also handles targetCategoryIndex == -1 (uncategorized) for free.
    for (auto& cat : categories)
    {
        auto it = std::find(cat.members.begin(), cat.members.end(), memberName);
        if (it != cat.members.end())
            cat.members.erase(it);
    }

    if (targetCategoryIndex >= 0 && targetCategoryIndex < (int)categories.size())
        categories[targetCategoryIndex].members.push_back(memberName);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeCategory / DeserializeCategory / SerializeCategoryList / DeserializeCategoryList
//--------------------------------------------------------------------------------
// Category <-> json conversion; the List variants just map the single-item
// versions over a json array. DeserializeCategory defaults a missing name to
// "Unnamed Category" and a missing members array to empty.
//--------------------------------------------------------------------------------
static json SerializeCategory(const Category& cat)
{
    json j;
    j["name"]    = cat.name;
    j["members"] = cat.members;
    return j;
}

static Category DeserializeCategory(const json& j)
{
    Category cat;
    cat.name    = j.value("name", std::string("Unnamed Category"));
    cat.members = j.value("members", std::vector<std::string>{});
    return cat;
}

static json SerializeCategoryList(const std::vector<Category>& categories)
{
    json arr = json::array();
    for (const auto& cat : categories)
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
    cat.name = def.name;
    for (const auto& m : def.members)
        cat.members.push_back(m.name);
    return cat;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeCategoryDefaults
//--------------------------------------------------------------------------------
// Same idea as MergeByKey in events_storage.cpp, keyed by category name - but a
// Category's only content is its member list, so "JSON wins" is the whole merge
// for a matched name, not just a starting point.
//
// Name in both: keep the JSON category as-is. Name only in defaults: added only
// when resurrectMissingDefaults is true (an up-to-date file treats a missing
// default as user-deleted, not new). Name only in JSON: always kept.
//--------------------------------------------------------------------------------
static std::vector<Category> MergeCategoryDefaults(const std::vector<CategoryDefault>& defaults, const std::vector<Category>& loaded, bool resurrectMissingDefaults)
{
    std::unordered_map<std::string, size_t> loadedIndexByName;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByName[loaded[i].name] = i;   //. last one wins on repeats

    std::vector<bool> matched(loaded.size(), false);
    std::vector<Category> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndexByName.find(def.name);
        if (it != loadedIndexByName.end())
        {
            result.push_back(loaded[it->second]);
            matched[it->second] = true;
        }
        else if (resurrectMissingDefaults)
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
// unconditionally places memberName into categoryName, removing it from every
// other category first. Creates categoryName if it isn't in the merged list yet
// (defensive; shouldn't normally happen). Only called once the caller has
// confirmed the file predates EVENTS_DATA_VERSION.
//--------------------------------------------------------------------------------
static void ForceCategoryMembership(std::vector<Category>& categories, const std::string& categoryName, const std::string& memberName)
{
    for (auto& cat : categories)
    {
        auto it = std::find(cat.members.begin(), cat.members.end(), memberName);
        if (it != cat.members.end())
            cat.members.erase(it);
    }

    for (auto& cat : categories)
    {
        if (cat.name == categoryName)
        {
            cat.members.push_back(memberName);
            return;
        }
    }

    categories.push_back({categoryName, {memberName}});
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveCategoriesData / LoadCategoriesData
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

        j["basicCategories"]  = SerializeCategoryList(g_BasicCategories);
        j["cyclicCategories"] = SerializeCategoryList(g_CyclicCategories);

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

        //_ Stays 0 with empty loaded lists when there's no file yet, so resurrectMissingDefaults naturally resolves to the defaults.
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

        //_ Same rule as LoadEventsData: an up-to-date file means the user's own edits win, so forced members aren't re-applied.
        bool resurrectMissingDefaults = savedVersion < EVENTS_DATA_VERSION;

        g_BasicCategories  = MergeCategoryDefaults(g_DefaultBasicCategories,  loadedBasic,  resurrectMissingDefaults);
        g_CyclicCategories = MergeCategoryDefaults(g_DefaultCyclicCategories, loadedCyclic, resurrectMissingDefaults);

        if (resurrectMissingDefaults)
        {
            for (const auto& def : g_DefaultBasicCategories)
                for (const auto& m : def.members)
                    if (m.forced)
                        ForceCategoryMembership(g_BasicCategories, def.name, m.name);

            for (const auto& def : g_DefaultCyclicCategories)
                for (const auto& m : def.members)
                    if (m.forced)
                        ForceCategoryMembership(g_CyclicCategories, def.name, m.name);
        }

        return fileExisted;
    }
    catch (...) { return false; }
}