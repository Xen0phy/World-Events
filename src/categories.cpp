// categories.cpp
// Storage and JSON persistence for user-created category groupings.
//
// Unlike g_Events/g_CyclicGroups, categories have no compiled-in defaults
// to merge against — they're entirely user-created, so loading just
// replaces whatever's in memory with whatever's on disk. No merge step,
// no name-collision handling beyond what RenameCategoryMember already does.
//
// Persisted in events.json alongside "events"/"cyclicGroups", as two
// sibling arrays: "basicCategories" and "cyclicCategories". This keeps the
// existing events/cyclicGroups arrays completely untouched — categories
// only ever reference names that live there, never copies of the actual
// data.

#include "categories.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<Category> g_BasicCategories;
std::vector<Category> g_CyclicCategories;

// ---------------------------------------------------------------------------
// RenameCategoryMember
// ---------------------------------------------------------------------------
void RenameCategoryMember(std::vector<Category>& categories, const std::string& oldName, const std::string& newName)
{
    if (oldName == newName) return;

    for (auto& cat : categories)
        for (auto& member : cat.members)
            if (member == oldName)
                member = newName;
            // Deliberately no break/return here: if oldName somehow
            // appears more than once (e.g. in two different categories,
            // or twice within one due to a prior data error), every
            // occurrence gets patched, not just the first found.
}

// ---------------------------------------------------------------------------
// MoveCategoryMember
// ---------------------------------------------------------------------------
void MoveCategoryMember(std::vector<Category>& categories, const std::string& memberName, int targetCategoryIndex)
{
    // Remove from every category first — membership is exclusive, so
    // wherever it currently lives (if anywhere) needs to be cleared
    // before it can be added to the new target. This also correctly
    // handles targetCategoryIndex == -1 (uncategorized): remove, add
    // nowhere, done.
    for (auto& cat : categories)
    {
        auto it = std::find(cat.members.begin(), cat.members.end(), memberName);
        if (it != cat.members.end())
            cat.members.erase(it);
    }

    if (targetCategoryIndex >= 0 && targetCategoryIndex < (int)categories.size())
        categories[targetCategoryIndex].members.push_back(memberName);
}

// ---------------------------------------------------------------------------
// (De)serialization
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// SaveCategoriesData / LoadCategoriesData
// ---------------------------------------------------------------------------
// These read/write the SAME events.json file that SaveEventsData/
// LoadEventsData use (see events_storage.cpp), adding/reading two extra
// top-level keys without disturbing "events"/"cyclicGroups"/"data_version".
// Both are called right alongside the existing SaveEventsData/
// LoadEventsData calls in addon.cpp.
//
// Like the other Save*/Load* functions in this codebase, both swallow
// exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveCategoriesData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

        // Read whatever's already there first (events/cyclicGroups/
        // data_version, written by SaveEventsData), so this save only
        // adds/updates the category keys without clobbering the rest of
        // the file. If the file doesn't exist yet, start from an empty
        // object — SaveEventsData is expected to run first in practice
        // (see addon.cpp), but this stays safe either way.
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
        std::ifstream file(filepath);
        if (!file.is_open()) return false; // no file yet — categories stay empty

        json j = json::parse(file);

        if (j.contains("basicCategories"))
            g_BasicCategories = DeserializeCategoryList(j["basicCategories"]);
        if (j.contains("cyclicCategories"))
            g_CyclicCategories = DeserializeCategoryList(j["cyclicCategories"]);

        return true;
    }
    catch (...) { return false; }
}
