//################################################################################
// events_tracking.cpp
//--------------------------------------------------------------------------------
// See events_tracking.h for scope/rationale. Storage and JSON persistence
// for the manually-marked "done for today" flags.
//
// Structurally mirrors subscriptions.cpp closely (same two-vector, same
// key shape, same events.json read-modify-write pattern); the difference
// is the stored UTC-day stamp and the lazy rollover check on every read,
// which subscriptions.cpp has no equivalent of since a subscription
// doesn't expire on its own.
//--------------------------------------------------------------------------------

#include "events_tracking.h"
#include "events.h"
#include "nlohmann_json.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

//_ Local storage backing Is/Toggle*DoneToday (events_tracking.h).
static std::vector<std::string>           s_DoneTodayBasicEvents;
static std::vector<CyclicSubscriptionKey>  s_DoneTodayCyclicSlots;

//_ See GetDoneMarkersGeneration's comment in events_tracking.h.
static uint64_t s_doneMarkersGeneration = 0;
uint64_t GetDoneMarkersGeneration() { return s_doneMarkersGeneration; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CurrentUtcDay
//--------------------------------------------------------------------------------
// Same one-line derivation as gw2_api.cpp's CurrentUtcDay() - duplicated
// locally rather than shared for a single division; see that file's
// comment for why floor-dividing Unix time needs no timezone handling.
//--------------------------------------------------------------------------------
static long long CurrentUtcDay()
{
    return (long long)(time(nullptr) / 86400);
}

//_ -1 = never loaded/saved; the first check of the day then treats that
// as stale and clears (a no-op - vectors start empty), no separate flag.
static long long s_DoneTodayUtcDay = -1;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RollOverIfNewUtcDay
//--------------------------------------------------------------------------------
// Called at the top of every read/write entry point below. Cheap: just an
// integer compare in the common case where the day hasn't rolled over. No
// timer, no per-frame poll - checking lazily on access means no missed
// frame can leave yesterday's marks visible past reset.
//--------------------------------------------------------------------------------
static void RollOverIfNewUtcDay()
{
    long long today = CurrentUtcDay();
    if (s_DoneTodayUtcDay == today) return;

    s_DoneTodayUtcDay = today;
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResolveBasicDoneKey
//--------------------------------------------------------------------------------
// Maps a Basic Event's own name to the key its "done today" mark is
// actually stored/looked-up under: g_Events[name].doneGroup if that event
// has one set, else the name itself unchanged. See WorldEvent::doneGroup
// (events.h) and this file's header comment for the Ley Line Anomaly case
// this exists for.
//
// Plain linear scan over g_Events - same cost class as the lookups
// GetDefaultEvent (events_storage.cpp) already does for the options panel,
// and this runs on the same rare "user right-clicked a row" path, not
// per-frame.
//--------------------------------------------------------------------------------
static std::string ResolveBasicDoneKey(const std::string& eventName)
{
    for (const auto& ev : g_Events)
    {
        if (ev.name != eventName) continue;
        return ev.doneGroup.empty() ? eventName : ev.doneGroup;
    }
    return eventName; //. unknown name (e.g. stale data) - fall back to itself
}

bool IsBasicEventMarkedDoneToday(const std::string& eventName)
{
    RollOverIfNewUtcDay();
    const std::string key = ResolveBasicDoneKey(eventName);
    return std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), key)
        != s_DoneTodayBasicEvents.end();
}

void ToggleBasicEventDoneToday(const std::string& eventName)
{
    RollOverIfNewUtcDay();
    const std::string key = ResolveBasicDoneKey(eventName);
    auto it = std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), key);
    if (it != s_DoneTodayBasicEvents.end())
        s_DoneTodayBasicEvents.erase(it);
    else
        s_DoneTodayBasicEvents.push_back(key);
    s_doneMarkersGeneration++;
}

bool IsCyclicSlotMarkedDoneToday(const CyclicSubscriptionKey& key)
{
    RollOverIfNewUtcDay();
    return std::find(s_DoneTodayCyclicSlots.begin(), s_DoneTodayCyclicSlots.end(), key)
        != s_DoneTodayCyclicSlots.end();
}

void ToggleCyclicSlotDoneToday(const CyclicSubscriptionKey& key)
{
    RollOverIfNewUtcDay();
    auto it = std::find(s_DoneTodayCyclicSlots.begin(), s_DoneTodayCyclicSlots.end(), key);
    if (it != s_DoneTodayCyclicSlots.end())
        s_DoneTodayCyclicSlots.erase(it);
    else
        s_DoneTodayCyclicSlots.push_back(key);
    s_doneMarkersGeneration++;
}

void ClearAllDoneMarkers()
{
    //_ Deliberately leaves s_DoneTodayUtcDay untouched - a manual reset,
    // not a day rollover, so the next natural rollover still happens on
    // schedule.
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeCyclicKey / DeserializeCyclicKey
//--------------------------------------------------------------------------------
// Same (groupName, slotOffset) key shape as subscriptions.cpp.
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveDailyTrackingData / LoadDailyTrackingData
//--------------------------------------------------------------------------------
bool SaveDailyTrackingData(const std::string& addonDir)
{
    RollOverIfNewUtcDay(); //. don't persist a stale day

    try
    {
        std::string filepath = addonDir + "\\events.json";

        //_ Read-modify-write, same reason as SaveSubscriptionsData - avoids
        // clobbering the file's other keys (events/categories/subscriptions).
        json j;
        {
            std::ifstream in(filepath);
            if (in.is_open())
            {
                try { j = json::parse(in); }
                catch (...) { j = json::object(); }
            }
        }

        j["doneTodayUtcDay"]        = s_DoneTodayUtcDay;
        j["doneTodayBasicEvents"]   = s_DoneTodayBasicEvents;

        json cyclicArr = json::array();
        for (const auto& key : s_DoneTodayCyclicSlots)
            cyclicArr.push_back(SerializeCyclicKey(key));
        j["doneTodayCyclicSlots"] = cyclicArr;

        fs::create_directories(addonDir);
        std::ofstream out(filepath);
        if (!out.is_open()) return false;
        out << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadDailyTrackingData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false; //. no file yet, stays empty

        json j = json::parse(file);

        long long storedDay = j.value("doneTodayUtcDay", (long long)-1);

        //_ Stale marks (addon closed across a reset) are skipped entirely -
        // s_DoneTodayUtcDay stays -1, so RollOverIfNewUtcDay() finds a clean day.
        if (storedDay != CurrentUtcDay())
            return true;

        s_DoneTodayUtcDay = storedDay;

        if (j.contains("doneTodayBasicEvents"))
            s_DoneTodayBasicEvents = j.value("doneTodayBasicEvents", std::vector<std::string>{});

        s_DoneTodayCyclicSlots.clear();
        if (j.contains("doneTodayCyclicSlots") && j["doneTodayCyclicSlots"].is_array())
            for (const auto& kj : j["doneTodayCyclicSlots"])
                s_DoneTodayCyclicSlots.push_back(DeserializeCyclicKey(kj));

        s_doneMarkersGeneration++;
        return true;
    }
    catch (...) { return false; }
}