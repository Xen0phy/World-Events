// events_tracking.cpp
// Storage and JSON persistence for manually-marked "done for today" flags.
// See events_tracking.h for the overall rationale.
//
// Structurally this mirrors subscriptions.cpp closely (same two-vector,
// same key shape, same events.json read-modify-write pattern) — the
// difference is the stored UTC-day stamp and the lazy rollover check on
// every read, which subscriptions.cpp has no equivalent of since a
// subscription doesn't expire on its own.

#include "events_tracking.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::vector<std::string>           s_DoneTodayBasicEvents;
static std::vector<CyclicSubscriptionKey>  s_DoneTodayCyclicSlots;

// See GetDoneMarkersGeneration's comment in events_tracking.h.
static uint64_t s_doneMarkersGeneration = 0;
uint64_t GetDoneMarkersGeneration() { return s_doneMarkersGeneration; }

// Same one-line UTC-day derivation as gw2_api.cpp's CurrentUtcDay() —
// duplicated locally rather than shared across modules for a single
// division, same as that file's own comment on why floor-division of
// Unix time lines up with UTC daily reset with no timezone handling
// needed.
static long long CurrentUtcDay()
{
    return (long long)(time(nullptr) / 86400);
}

// -1 = never loaded/saved yet, so the very first check of the day treats
// that as "stale" and clears (a no-op, since both vectors start empty)
// rather than needing a separate "initialized" flag.
static long long s_DoneTodayUtcDay = -1;

// Called at the top of every read/write entry point below. Cheap: just an
// integer compare in the overwhelmingly common case where the day hasn't
// rolled over. No timer, no per-frame poll — checking lazily on access
// means there's no window where a missed frame could leave yesterday's
// marks visible past reset.
static void RollOverIfNewUtcDay()
{
    long long today = CurrentUtcDay();
    if (s_DoneTodayUtcDay == today) return;

    s_DoneTodayUtcDay = today;
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_doneMarkersGeneration++;
}

// ---------------------------------------------------------------------------
// Basic Events
// ---------------------------------------------------------------------------
bool IsBasicEventMarkedDoneToday(const std::string& eventName)
{
    RollOverIfNewUtcDay();
    return std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), eventName)
        != s_DoneTodayBasicEvents.end();
}

void ToggleBasicEventDoneToday(const std::string& eventName)
{
    RollOverIfNewUtcDay();
    auto it = std::find(s_DoneTodayBasicEvents.begin(), s_DoneTodayBasicEvents.end(), eventName);
    if (it != s_DoneTodayBasicEvents.end())
        s_DoneTodayBasicEvents.erase(it);
    else
        s_DoneTodayBasicEvents.push_back(eventName);
    s_doneMarkersGeneration++;
}

// ---------------------------------------------------------------------------
// Cyclic slots
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Manual reset button (options panel)
// ---------------------------------------------------------------------------
void ClearAllDoneMarkers()
{
    // Deliberately does NOT touch s_DoneTodayUtcDay — this is a manual
    // "I want a clean slate right now" action, not a day rollover, so the
    // next natural rollover still happens on its own schedule afterward
    // rather than being reset to "never happened yet".
    s_DoneTodayBasicEvents.clear();
    s_DoneTodayCyclicSlots.clear();
    s_doneMarkersGeneration++;
}

// ---------------------------------------------------------------------------
// (De)serialization — same key shape as subscriptions.cpp
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
// SaveDailyTrackingData / LoadDailyTrackingData
// ---------------------------------------------------------------------------
bool SaveDailyTrackingData(const std::string& addonDir)
{
    RollOverIfNewUtcDay(); // never persist a stale day's marks

    try
    {
        std::string filepath = addonDir + "\\events.json";

        // Read-modify-write, same reason as SaveSubscriptionsData: don't
        // clobber events/cyclicGroups/categories/subscriptions already in
        // the file.
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
        if (!file.is_open()) return false; // no file yet — stays empty

        json j = json::parse(file);

        long long storedDay = j.value("doneTodayUtcDay", (long long)-1);

        // If the stored marks are from a previous UTC day (addon was
        // closed across a daily reset), don't load them at all — leave
        // s_DoneTodayUtcDay at -1 so the next access rolls over into a
        // clean, correctly-dated empty state on its own via
        // RollOverIfNewUtcDay(), rather than loading stale entries just
        // to immediately clear them.
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
