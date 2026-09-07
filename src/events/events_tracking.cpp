//################################################################################
// events_tracking.cpp
//--------------------------------------------------------------------------------
// See events_tracking.h for scope/rationale. Storage and JSON persistence for the
// manually-marked "done for today" flags.
//
// Structurally mirrors subscriptions.cpp closely (same two-vector, same key
// shape, same events.json read-modify-write pattern); the difference is the
// stored UTC-day stamp and the lazy rollover check on every read, which
// subscriptions.cpp has no equivalent of since a subscription doesn't expire on
// its own.
//--------------------------------------------------------------------------------

#include "events_tracking.h"
#include "events.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;
namespace fs = std::filesystem;

//_ Local storage backing Is/Toggle*DoneToday (events_tracking.h).
static std::vector<std::string>           s_DoneTodayBasicEvents;
static std::vector<CyclicSubscriptionKey>  s_DoneTodayCyclicSlots;
static std::vector<std::string>           s_DoneTodayLiveEvents;

//_ See GetDoneMarkersGeneration's comment in events_tracking.h.
static uint64_t s_doneMarkersGeneration = 0;
uint64_t GetDoneMarkersGeneration() { return s_doneMarkersGeneration; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CurrentUtcDay
//--------------------------------------------------------------------------------
// Same one-line derivation as gw2_api.cpp's CurrentUtcDay() - duplicated locally,
// not shared, for a single division; see that file's comment for why floor-
// dividing Unix time needs no timezone handling.
//--------------------------------------------------------------------------------
static long long CurrentUtcDay()
{
    return (long long)(time(nullptr) / 86400);
}

//_ -1 = never loaded/saved; first check clears it as stale (harmless no-op).
static long long s_DoneTodayUtcDay = -1;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RollOverIfNewUtcDay
//--------------------------------------------------------------------------------
// Called at the top of every read/write entry point below. Cheap: just an integer
// compare in the common case where the day hasn't rolled over. No timer, no per-
// frame poll - checking lazily on access means no missed frame can leave
// yesterday's marks visible past reset.
//--------------------------------------------------------------------------------
static void RollOverIfNewUtcDay()
{
    long long today = CurrentUtcDay();
    if (s_DoneTodayUtcDay == today) return;

    s_DoneTodayUtcDay = today;
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_DoneTodayLiveEvents.clear();
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResolveBasicDoneKey
//--------------------------------------------------------------------------------
// Maps a Basic Event's own id to the key its "done today" mark is actually
// stored/looked-up under: g_Events[id].doneGroup if that event has one set,
// else the id itself unchanged. See WorldEvent::doneGroup (events.h) and this
// file's header comment for the Ley Line Anomaly case this exists for.
//
// Plain linear scan over g_Events - same cost class as the lookups
// GetDefaultEvent (events_storage.cpp) already does for the options panel, and
// this runs on the same rare "user right-clicked a row" path, not per-frame.
//--------------------------------------------------------------------------------
static std::string ResolveBasicDoneKey(const std::string& eventId)
{
    for (const auto& ev : g_Events)
    {
        if (ev.id != eventId) continue;
        return ev.doneGroup.empty() ? eventId : ev.doneGroup;
    }
    return eventId; //. stale/unknown id - unchanged
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventMarkedDoneToday / ToggleBasicEventDoneToday (see: events_tracking.h)
//--------------------------------------------------------------------------------
bool IsBasicEventMarkedDoneToday(const std::string& eventId)
{
    RollOverIfNewUtcDay();
    const std::string key = ResolveBasicDoneKey(eventId);
    return std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), key)
        != s_DoneTodayBasicEvents.end();
}

void ToggleBasicEventDoneToday(const std::string& eventId)
{
    RollOverIfNewUtcDay();
    const std::string key = ResolveBasicDoneKey(eventId);
    auto it = std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), key);
    if (it != s_DoneTodayBasicEvents.end())
        s_DoneTodayBasicEvents.erase(it);
    else
        s_DoneTodayBasicEvents.push_back(key);
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotMarkedDoneToday / ToggleCyclicSlotDoneToday (see: events_tracking.h)
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsLiveEventMarkedDoneToday / ToggleLiveEventDoneToday   (see: events_tracking.h)
//--------------------------------------------------------------------------------
bool IsLiveEventMarkedDoneToday(const std::string& eventId)
{
    RollOverIfNewUtcDay();
    return std::find(s_DoneTodayLiveEvents.begin(), s_DoneTodayLiveEvents.end(), eventId)
        != s_DoneTodayLiveEvents.end();
}

void ToggleLiveEventDoneToday(const std::string& eventId)
{
    RollOverIfNewUtcDay();
    auto it = std::find(s_DoneTodayLiveEvents.begin(), s_DoneTodayLiveEvents.end(), eventId);
    if (it != s_DoneTodayLiveEvents.end())
        s_DoneTodayLiveEvents.erase(it);
    else
        s_DoneTodayLiveEvents.push_back(eventId);
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearAllDoneMarkers   (see: events_tracking.h)
//--------------------------------------------------------------------------------
void ClearAllDoneMarkers()
{
    //_ Leaves s_DoneTodayUtcDay untouched - a manual reset, not a rollover.
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_DoneTodayLiveEvents.clear();
    s_doneMarkersGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeCyclicKey / DeserializeCyclicKey
//--------------------------------------------------------------------------------
// Same (groupId, slotOffset) key shape as subscriptions.cpp, including the same
// legacy-"groupName"-field fallback on read for a pre-id-migration events.json.
//--------------------------------------------------------------------------------
static json SerializeCyclicKey(const CyclicSubscriptionKey& key)
{
    json j;
    j["groupId"]    = key.groupId;
    j["slotOffset"] = key.slotOffset;
    return j;
}

static CyclicSubscriptionKey DeserializeCyclicKey(const json& j)
{
    CyclicSubscriptionKey key;
    key.groupId    = j.value("groupId", j.value("groupName", std::string()));
    key.slotOffset = j.value("slotOffset", 0);
    return key;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MigrateBasicMarksToIds / MigrateCyclicMarksToIds
//--------------------------------------------------------------------------------
// One-time upgrade for a pre-id-migration events.json, whose done-today marks
// are still WorldEvent::name/CyclicGroup::name values. Self-triggering, no
// version gate: an entry already found in validIds is left alone (also true of
// any doneGroup value, which was never name-shaped to begin with); only an
// entry that misses as an id but hits nameToId gets rewritten. An entry
// matching neither (a doneGroup value, or a removed event/group) is left as-is
// - mirrors MigrateMembersToIds (events_categories.cpp).
//--------------------------------------------------------------------------------
static void MigrateBasicMarksToIds(std::vector<std::string>& list, const std::unordered_map<std::string, std::string>& nameToId, const std::unordered_set<std::string>& validIds)
{
    for (auto& value : list)
    {
        if (validIds.count(value)) continue;

        auto it = nameToId.find(value);
        if (it != nameToId.end())
            value = it->second;
    }
}

static void MigrateCyclicMarksToIds(std::vector<CyclicSubscriptionKey>& list, const std::unordered_map<std::string, std::string>& nameToId, const std::unordered_set<std::string>& validIds)
{
    for (auto& key : list)
    {
        if (validIds.count(key.groupId)) continue;

        auto it = nameToId.find(key.groupId);
        if (it != nameToId.end())
            key.groupId = it->second;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveDailyTrackingData / LoadDailyTrackingData   (see: events_tracking.h)
//--------------------------------------------------------------------------------
bool SaveDailyTrackingData(const std::string& addonDir)
{
    RollOverIfNewUtcDay(); //. don't persist a stale day

    try
    {
        std::string filepath = addonDir + "\\events.json";

        //_ Read-modify-write - avoids clobbering the other JSON keys.
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

        j["doneTodayLiveEvents"] = s_DoneTodayLiveEvents;

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

        //_ Stale marks (old UTC day) are skipped, leaving a clean day.
        if (storedDay != CurrentUtcDay())
            return true;

        s_DoneTodayUtcDay = storedDay;

        if (j.contains("doneTodayBasicEvents"))
            s_DoneTodayBasicEvents = j.value("doneTodayBasicEvents", std::vector<std::string>{});

        s_DoneTodayCyclicSlots.clear();
        if (j.contains("doneTodayCyclicSlots") && j["doneTodayCyclicSlots"].is_array())
            for (const auto& kj : j["doneTodayCyclicSlots"])
                s_DoneTodayCyclicSlots.push_back(DeserializeCyclicKey(kj));

        if (j.contains("doneTodayLiveEvents"))
            s_DoneTodayLiveEvents = j.value("doneTodayLiveEvents", std::vector<std::string>{});

        //_ Requires g_Events/g_CyclicGroups already populated - see LoadDailyTrackingData's own comment (events_tracking.h) on load order.
        std::unordered_map<std::string, std::string> eventNameToId;
        std::unordered_set<std::string> eventIds;
        for (const auto& ev : g_Events)
        {
            eventNameToId[ev.name] = ev.id;
            eventIds.insert(ev.id);
        }
        MigrateBasicMarksToIds(s_DoneTodayBasicEvents, eventNameToId, eventIds);

        std::unordered_map<std::string, std::string> groupNameToId;
        std::unordered_set<std::string> groupIds;
        for (const auto& grp : g_CyclicGroups)
        {
            groupNameToId[grp.name] = grp.id;
            groupIds.insert(grp.id);
        }
        MigrateCyclicMarksToIds(s_DoneTodayCyclicSlots, groupNameToId, groupIds);

        s_doneMarkersGeneration++;
        return true;
    }
    catch (...) { return false; }
}