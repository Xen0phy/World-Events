// subscriptions.cpp
// Storage and JSON persistence for the user's subscribed-events watchlist.
//
// Mirrors categories.cpp closely: no compiled-in defaults to merge
// against, so loading just replaces whatever's in memory with whatever's
// on disk. Persisted in events.json alongside "events"/"cyclicGroups"/
// "basicCategories"/"cyclicCategories", as two more sibling keys.

#include "subscriptions.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<std::string>            g_SubscribedBasicEvents;
std::vector<CyclicSubscriptionKey>  g_SubscribedCyclicSlots;

// ---------------------------------------------------------------------------
// Basic Event subscriptions
// ---------------------------------------------------------------------------
bool IsBasicEventSubscribed(const std::string& eventName)
{
    return std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), eventName)
        != g_SubscribedBasicEvents.end();
}

void ToggleBasicEventSubscription(const std::string& eventName)
{
    auto it = std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), eventName);
    if (it != g_SubscribedBasicEvents.end())
        g_SubscribedBasicEvents.erase(it);
    else
        g_SubscribedBasicEvents.push_back(eventName);
}

void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName)
{
    if (oldName == newName) return;

    for (auto& name : g_SubscribedBasicEvents)
        if (name == oldName)
            name = newName;
        // Deliberately no break — same reasoning as RenameCategoryMember:
        // patch every occurrence, not just the first, in case of a prior
        // data inconsistency.
}

// ---------------------------------------------------------------------------
// Cyclic slot subscriptions
// ---------------------------------------------------------------------------
bool IsCyclicSlotSubscribed(const CyclicSubscriptionKey& key)
{
    return std::find(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(), key)
        != g_SubscribedCyclicSlots.end();
}

void ToggleCyclicSlotSubscription(const CyclicSubscriptionKey& key)
{
    auto it = std::find(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(), key);
    if (it != g_SubscribedCyclicSlots.end())
        g_SubscribedCyclicSlots.erase(it);
    else
        g_SubscribedCyclicSlots.push_back(key);
}

// ---------------------------------------------------------------------------
// (De)serialization
// ---------------------------------------------------------------------------
static json SerializeCyclicKey(const CyclicSubscriptionKey& key)
{
    json j;
    j["groupName"]  = key.groupName;
    j["slotOffset"] = key.slotOffset;
    return j;
}

static CyclicSubscriptionKey DeserializeCyclicKey(const json& j)
{
    CyclicSubscriptionKey key;
    key.groupName  = j.value("groupName", std::string());
    key.slotOffset = j.value("slotOffset", 0);
    return key;
}

// ---------------------------------------------------------------------------
// SaveSubscriptionsData / LoadSubscriptionsData
// ---------------------------------------------------------------------------
bool SaveSubscriptionsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

        // Read whatever's already there first (events/cyclicGroups/
        // categories/data_version), so this save only adds/updates the
        // subscription keys without clobbering the rest of the file —
        // same pattern as SaveCategoriesData, and for the same reason.
        json j;
        {
            std::ifstream in(filepath);
            if (in.is_open())
            {
                try { j = json::parse(in); }
                catch (...) { j = json::object(); }
            }
        }

        j["subscribedBasicEvents"] = g_SubscribedBasicEvents;

        json cyclicArr = json::array();
        for (const auto& key : g_SubscribedCyclicSlots)
            cyclicArr.push_back(SerializeCyclicKey(key));
        j["subscribedCyclicSlots"] = cyclicArr;

        fs::create_directories(addonDir);
        std::ofstream out(filepath);
        if (!out.is_open()) return false;
        out << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadSubscriptionsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false; // no file yet — subscriptions stay empty

        json j = json::parse(file);

        if (j.contains("subscribedBasicEvents"))
            g_SubscribedBasicEvents = j.value("subscribedBasicEvents", std::vector<std::string>{});

        g_SubscribedCyclicSlots.clear();
        if (j.contains("subscribedCyclicSlots") && j["subscribedCyclicSlots"].is_array())
            for (const auto& kj : j["subscribedCyclicSlots"])
                g_SubscribedCyclicSlots.push_back(DeserializeCyclicKey(kj));

        return true;
    }
    catch (...) { return false; }
}
