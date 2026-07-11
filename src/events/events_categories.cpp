// events_categories.cpp
// Storage and JSON persistence for category groupings.
//
// Categories can come from two places: compiled-in defaults
// (g_DefaultBasicCategories/g_DefaultCyclicCategories, written by hand in
// events_basic.cpp/events_cyclic.cpp — see events_categories.h) and user-created
// ones (made through the options-panel drag-and-drop UI). Loading merges
// the two by category NAME, the same way LoadEventsData merges g_Events/
// g_CyclicGroups in events_storage.cpp — see MergeCategoryDefaults below.
//
// Persisted in events.json alongside "events"/"cyclicGroups", as two
// sibling arrays: "basicCategories" and "cyclicCategories". This keeps the
// existing events/cyclicGroups arrays completely untouched — categories
// only ever reference names that live there, never copies of the actual
// data.

#include "events_categories.h"
#include "events.h" // EVENTS_DATA_VERSION — shared version number for the whole events.json file
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>

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
// Merging compiled-in default categories with what's on disk
// ---------------------------------------------------------------------------
// A CategoryDefault only carries plain names + a `forced` flag (see
// events_categories.h) — this converts one down to a runtime Category so it can
// be dropped straight into g_BasicCategories/g_CyclicCategories when no
// JSON version exists yet to win instead.
static Category CategoryDefaultToCategory(const CategoryDefault& def)
{
    Category cat;
    cat.name = def.name;
    for (const auto& m : def.members)
        cat.members.push_back(m.name);
    return cat;
}

// Same shape as MergeByKey in events_storage.cpp, just not the same
// template — the element types differ (CategoryDefault vs Category), and
// unlike events/groups there's nothing to merge one level deeper (a
// Category's only content IS its member list, so "JSON wins" is the whole
// merge for a matched name, not just a starting point).
//
//   - Name in both      -> keep the JSON category as-is (its membership,
//                           however the user has arranged it, wins).
//   - Name only in defaults -> new compiled-in category. Added only when
//                           resurrectMissingDefaults is true; on an
//                           up-to-date file, a missing default name means
//                           the user deleted that category, not that it's
//                           new.
//   - Name only in JSON -> a category the user made themselves (or one
//                           that used to be compiled-in and got removed
//                           from the build) — always kept, never dropped.
static std::vector<Category> MergeCategoryDefaults(const std::vector<CategoryDefault>& defaults, const std::vector<Category>& loaded, bool resurrectMissingDefaults)
{
    std::unordered_map<std::string, size_t> loadedIndexByName;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByName[loaded[i].name] = i; // last one wins if names somehow repeat in the file

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
        // else: dropped — file is current, so this default's absence means
        // the user deleted the category, not that it's newly-shipped.
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

// ---------------------------------------------------------------------------
// ForceCategoryMembership
// ---------------------------------------------------------------------------
// Implements CategoryDefaultMember::forced: unconditionally places
// memberName into the category named categoryName, removing it from every
// OTHER category in the list first (membership stays exclusive — same rule
// MoveCategoryMember already follows). If categoryName isn't in the merged
// list yet for some reason (e.g. its own default was dropped because the
// file was already current — shouldn't normally happen together with a
// forced member still being processed, but handled defensively), the
// category is created so the forced member always has somewhere to land.
//
// Only ever called when the caller has already confirmed the file predates
// EVENTS_DATA_VERSION — see LoadCategoriesData below and
// CategoryDefaultMember::forced in events_categories.h.
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

        // savedVersion stays 0 and both loaded lists stay empty when there's
        // no file yet — which naturally makes resurrectMissingDefaults true
        // below and the merge resolve to exactly the compiled-in defaults,
        // matching how g_Events/g_CyclicGroups behave on a first-ever run.
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

        // Same rule as LoadEventsData (events_storage.cpp): only treat a
        // default missing from the file as new/shipped content — and only
        // re-assert a forced member's placement — when the file genuinely
        // predates this build. An up-to-date file means the user's own
        // edits (deleting a category, moving a forced member elsewhere)
        // are the current truth and shouldn't be fought on every load.
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
