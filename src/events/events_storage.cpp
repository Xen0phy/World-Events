//################################################################################
// events_storage.cpp
//--------------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in
// one file: "<addonDir>/events.json".
//
// File shape:
//   {
//     "data_version": 1,
//     "events": [ {...WorldEvent...}, ... ],
//     "cyclicGroups": [ {...CyclicGroup, with nested "slots": [...]...}, ... ]
//   }
//
// On load, the compiled-in defaults are merged with whatever's on disk,
// matched by key rather than replaced outright - see MergeByKey/
// MergeGroups below for the exact rule and why a plain name isn't always
// a safe key. The merged result becomes g_Events/g_CyclicGroups and is
// also what gets written back to disk afterward, so a first run (no file
// yet) writes out exactly the compiled-in defaults, and every run after
// that keeps merging forward.
//
// EVENTS_DATA_VERSION (events.h) gates that merge and is shared with
// events_categories.cpp, since both read/write the same "data_version"
// key in this same events.json.
//
// All functions swallow exceptions and return false on failure, matching
// the conventions in settings.cpp.
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_categories.h"
#include "nlohmann_json.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_map>

using json = nlohmann::json;
namespace fs = std::filesystem;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeEvent / DeserializeEvent
//--------------------------------------------------------------------------------
// (De)serializes one WorldEvent. iconTexture/chatCode are omitted when
// empty and shown is omitted when true (the default) - all three fall
// through cleanly via j.value() on load. isVarying selects varyingTimes
// vs period/offset (see WorldEvent in events.h).
//--------------------------------------------------------------------------------
static json SerializeEvent(const WorldEvent& ev)
{
    json j;
    j["name"]       = ev.name;
    j["continentX"] = ev.continentX;
    j["continentY"] = ev.continentY;
    j["isVarying"]  = ev.isVarying;
    j["duration"]   = ev.duration;

    if (!ev.iconTexture.empty())
        j["iconTexture"] = ev.iconTexture;

    if (!ev.chatCode.empty())
        j["chatCode"] = ev.chatCode;

    if (!ev.shown)
        j["shown"] = false;

    if (ev.isVarying)
        j["varyingTimes"] = ev.varyingTimes;
    else
    {
        j["period"] = ev.period;
        j["offset"] = ev.offset;
    }
    return j;
}

static WorldEvent DeserializeEvent(const json& j)
{
    WorldEvent ev{};
    ev.name        = j.value("name", std::string("Unnamed Event"));
    ev.continentX  = j.value("continentX", 0.0f);
    ev.continentY  = j.value("continentY", 0.0f);
    ev.isVarying   = j.value("isVarying", false);
    ev.duration    = j.value("duration", 0);
    ev.iconTexture = j.value("iconTexture", std::string());
    ev.chatCode    = j.value("chatCode", std::string());
    ev.shown       = j.value("shown", true);

    if (ev.isVarying)
        ev.varyingTimes = j.value("varyingTimes", std::vector<int>{});
    else
    {
        ev.period = j.value("period", 7200);
        ev.offset = j.value("offset", 0);
    }
    return ev;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ColorTierToString / ColorTierFromString
//--------------------------------------------------------------------------------
// Converts ColorTier to/from its JSON string ("Primary"/"Secondary"/
// "Tertiary"); unrecognized strings fall back to Primary.
//--------------------------------------------------------------------------------
static const char* ColorTierToString(ColorTier t)
{
    switch (t)
    {
        case ColorTier::Secondary: return "Secondary";
        case ColorTier::Tertiary:  return "Tertiary";
        default:                   return "Primary";
    }
}

static ColorTier ColorTierFromString(const std::string& s)
{
    if (s == "Secondary") return ColorTier::Secondary;
    if (s == "Tertiary")  return ColorTier::Tertiary;
    return ColorTier::Primary;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ColorToHexString / HexStringToColor
//--------------------------------------------------------------------------------
// Converts a packed ImU32 RGBA color to/from "#RRGGBBAA" hex, human-
// readable when hand-edited. Used for idleColor/customColor, which stay
// native ImU32 values in C++ and were never migrated to the float-array
// format ColorSet::base uses (see SerializeColorArray below). Falls back
// to `fallback` on any parse failure.
//--------------------------------------------------------------------------------
static std::string ColorToHexString(unsigned int rgba)
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%08X", rgba);
    return std::string(buf);
}

static unsigned int HexStringToColor(const std::string& s, unsigned int fallback)
{
    if (s.size() != 9 || s[0] != '#') return fallback;
    try { return (unsigned int)std::stoul(s.substr(1), nullptr, 16); }
    catch (...) { return fallback; }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeColorArray / DeserializeColorArray
//--------------------------------------------------------------------------------
// ColorSet::base is written as a plain [r, g, b, a] JSON array in [0,1],
// the same layout ImGui::ColorEdit4 reads/writes directly - no packing
// needed (contrast idleColor/customColor above, which stay packed ImU32).
// DeserializeColorArray also accepts the OLD "#RRGGBBAA" hex-string shape
// (ColorSet::base's format before this migration) as a one-time read
// fallback; no "needs resave" flag is needed since SaveAllData() already
// runs right after LoadEventsData() on every AddonLoad (addon.cpp), so
// the very next write re-serializes through this function and lands on
// the new format automatically.
//--------------------------------------------------------------------------------
static json SerializeColorArray(const ImVec4& c)
{
    return json::array({ c.x, c.y, c.z, c.w });
}

static ImVec4 DeserializeColorArray(const json& j, const ImVec4& fallback)
{
    if (j.is_array() && j.size() == 4)
        return ImVec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());

    if (j.is_string())
    {
        unsigned int rgba = HexStringToColor(j.get<std::string>(), 0x808080FFu);
        return ImVec4(((rgba >> 24) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f,
                      ((rgba >>  8) & 0xFF) / 255.0f, ( rgba        & 0xFF) / 255.0f);
    }

    return fallback;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeSlot / DeserializeSlot
//--------------------------------------------------------------------------------
// (De)serializes one CyclicGroup::Slot. customColor is presence-checked
// (j.contains) rather than defaulted, so "unset" round-trips exactly;
// chatCode is omitted when empty and shown when true (the default), same
// convention as WorldEvent above.
//--------------------------------------------------------------------------------
static json SerializeSlot(const CyclicGroup::Slot& slot)
{
    json j;
    j["name"]     = slot.name;
    j["offset"]   = slot.offset;
    j["duration"] = slot.duration;
    j["tier"]     = ColorTierToString(slot.tier);
    j["repeat"]   = slot.repeat;

    if (slot.customColor.has_value())
        j["customColor"] = ColorToHexString(*slot.customColor);

    if (!slot.chatCode.empty())
        j["chatCode"] = slot.chatCode;

    if (!slot.shown)
        j["shown"] = false;

    return j;
}

static CyclicGroup::Slot DeserializeSlot(const json& j)
{
    CyclicGroup::Slot slot{};
    slot.name     = j.value("name", std::string("Unnamed Event"));
    slot.offset   = j.value("offset", 0);
    slot.duration = j.value("duration", 0);
    slot.tier     = ColorTierFromString(j.value("tier", std::string("Primary")));
    slot.repeat   = j.value("repeat", 1);

    if (j.contains("customColor"))
        slot.customColor = HexStringToColor(j.value("customColor", std::string()), 0xFFFFFFFFu);

    slot.chatCode = j.value("chatCode", std::string());
    slot.shown    = j.value("shown", true);

    return slot;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeGroup / DeserializeGroup
//--------------------------------------------------------------------------------
// (De)serializes one CyclicGroup, including its nested slots array via
// SerializeSlot/DeserializeSlot. idleColor is presence-checked like
// Slot::customColor above; shown is omitted when true (the default), same
// convention as WorldEvent above.
//--------------------------------------------------------------------------------
static json SerializeGroup(const CyclicGroup& grp)
{
    json j;
    j["name"]       = grp.name;
    j["continentX"] = grp.continentX;
    j["continentY"] = grp.continentY;
    j["period"]     = grp.period;
    j["colors"]     = SerializeColorArray(grp.colors.base);

    if (grp.idleColor.has_value())
        j["idleColor"] = ColorToHexString(*grp.idleColor);

    if (!grp.shown)
        j["shown"] = false;

    json slots = json::array();
    for (const auto& slot : grp.slots)
        slots.push_back(SerializeSlot(slot));
    j["slots"] = slots;

    return j;
}

static CyclicGroup DeserializeGroup(const json& j)
{
    CyclicGroup grp{};
    grp.name       = j.value("name", std::string("Unnamed Group"));
    grp.continentX = j.value("continentX", 0.0f);
    grp.continentY = j.value("continentY", 0.0f);
    grp.period     = j.value("period", 7200);
    grp.colors     = ColorSet{ DeserializeColorArray(j.value("colors", json()), ImVec4(0.502f, 0.502f, 0.502f, 1.0f)) };   //. matches old #808080FF default

    if (j.contains("idleColor"))
        grp.idleColor = HexStringToColor(j.value("idleColor", std::string()), 0xFFFFFFFFu);

    grp.shown = j.value("shown", true);

    if (j.contains("slots") && j["slots"].is_array())
        for (const auto& sj : j["slots"])
            grp.slots.push_back(DeserializeSlot(sj));

    return grp;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeByKey
//--------------------------------------------------------------------------------
// Generic key-matched merge, used for top-level events/groups and for
// slots nested within a matched group. Preserves defaults' order first,
// then appends unmatched loaded entries in on-disk order. getKey must be
// unique within the list - plain name is NOT enough for slots, since
// e.g. Dry Top has two slots both named "Crash Site" at different
// offsets (see SlotKey below).
//
// resurrectMissingDefaults must be true only when the file predates
// EVENTS_DATA_VERSION; otherwise an unmatched default is treated as
// user-removed/renamed rather than new content - this is what prevents a
// rename from resurrecting the old name on the next load.
//--------------------------------------------------------------------------------
template<typename T, typename KeyFn>
static std::vector<T> MergeByKey(const std::vector<T>& defaults, const std::vector<T>& loaded, KeyFn getKey, bool resurrectMissingDefaults)
{
    std::unordered_map<std::string, size_t> loadedIndexByKey;
    //_ Last entry wins if getKey produced a duplicate key (shouldn't happen).
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByKey[getKey(loaded[i])] = i;

    std::vector<bool> matched(loaded.size(), false);
    std::vector<T> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndexByKey.find(getKey(def));
        if (it != loadedIndexByKey.end())
        {
            result.push_back(loaded[it->second]);
            matched[it->second] = true;
        }
        else if (resurrectMissingDefaults)
        {
            result.push_back(def);
        }
        //_ Dropped when false: file is current, so this default's absence
        // means the user removed/renamed it, not new content.
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GroupKey / SlotKey / EventKey
//--------------------------------------------------------------------------------
// Merge keys for MergeGroups/MergeByKey. Groups and events key on name
// alone (no duplicates exist in events_cyclic.cpp/events_basic.cpp today).
// Slots key on name+offset, since slot names CAN legitimately repeat
// within one group - e.g. Dry Top's two "Crash Site" slots at different
// offsets are a real repeated boss fight, not a typo.
//--------------------------------------------------------------------------------
static std::string GroupKey(const CyclicGroup& g) { return g.name; }
static std::string SlotKey(const CyclicGroup::Slot& s) { return s.name + "|" + std::to_string(s.offset); }
static std::string EventKey(const WorldEvent& e) { return e.name; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeGroups
//--------------------------------------------------------------------------------
// MergeByKey for CyclicGroup, plus one extra pass: even when a group
// matches by key and the loaded version wins overall, its slots are
// still merged one level deeper via MergeByKey, so a new slot added to
// that group in a newer build still appears instead of being replaced
// wholesale by the loaded group object. resurrectMissingDefaults is
// forwarded to both levels, so a slot the user deleted from an otherwise-
// matched group follows the same resurrect-or-drop rule as the group
// itself.
//--------------------------------------------------------------------------------
static std::vector<CyclicGroup> MergeGroups(const std::vector<CyclicGroup>& defaults, const std::vector<CyclicGroup>& loaded, bool resurrectMissingDefaults)
{
    std::unordered_map<std::string, size_t> loadedIndexByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByKey[GroupKey(loaded[i])] = i;

    std::vector<bool> matched(loaded.size(), false);
    std::vector<CyclicGroup> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndexByKey.find(GroupKey(def));
        if (it != loadedIndexByKey.end())
        {
            CyclicGroup merged = loaded[it->second];
            merged.slots = MergeByKey(def.slots, loaded[it->second].slots, SlotKey, resurrectMissingDefaults);
            result.push_back(merged);
            matched[it->second] = true;
        }
        else if (resurrectMissingDefaults)
        {
            result.push_back(def);
        }
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ApplyCategoryOffsetOverrides
//--------------------------------------------------------------------------------
// Implements CategoryDefaultMember::offset (events_categories.h). Reads
// straight from g_DefaultBasicCategories/g_DefaultCyclicCategories - the
// same compiled-in list events_basic.cpp/events_cyclic.cpp already
// maintain for category placement - so a schedule fix is a one-line edit
// right next to that member's category entry, no separate table to keep
// in sync. Same version gate as ForceCategoryMembership
// (events_categories.cpp): runs once, only while the saved file predates
// EVENTS_DATA_VERSION, then stops applying once the file's version
// catches up on the next save.
//--------------------------------------------------------------------------------
static void ApplyCategoryOffsetOverrides(std::vector<WorldEvent>& events, int64_t savedVersion)
{
    if (savedVersion >= EVENTS_DATA_VERSION) return;

    for (const auto& def : g_DefaultBasicCategories)
        for (const auto& m : def.members)
            if (m.offset.has_value())
                for (auto& ev : events)
                    if (ev.name == m.name)
                        ev.offset = *m.offset;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveEventsData / LoadEventsData
//--------------------------------------------------------------------------------
// SaveEventsData serializes g_Events/g_CyclicGroups straight to
// events.json (see file header for the shape). LoadEventsData instead
// merges them from disk via MergeByKey/MergeGroups, using
// resurrectMissingDefaults = (saved data_version < EVENTS_DATA_VERSION),
// then restamps apiWorldBossId/apiMapChestId from the compiled-in
// defaults, since those cross-reference fields are never read from or
// written to the file. A missing file isn't an error - g_Events/
// g_CyclicGroups are simply left at their compiled-in defaults; the
// caller (addon.cpp) is expected to call SaveEventsData right after so
// the file exists from then on.
//--------------------------------------------------------------------------------
bool SaveEventsData(const std::string& addonDir)
{
    try
    {
        fs::create_directories(addonDir);
        std::string filepath = addonDir + "\\events.json";

        json j;
        j["data_version"] = EVENTS_DATA_VERSION;

        json eventsArr = json::array();
        for (const auto& ev : g_Events)
            eventsArr.push_back(SerializeEvent(ev));
        j["events"] = eventsArr;

        json groupsArr = json::array();
        for (const auto& grp : g_CyclicGroups)
            groupsArr.push_back(SerializeGroup(grp));
        j["cyclicGroups"] = groupsArr;

        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadEventsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false;   //. no file yet, keep defaults

        json j = json::parse(file);

        int64_t savedVersion = j.value("data_version", (int64_t)0);
        bool resurrectMissingDefaults = savedVersion < EVENTS_DATA_VERSION;

        std::vector<WorldEvent> loadedEvents;
        if (j.contains("events") && j["events"].is_array())
            for (const auto& ej : j["events"])
                loadedEvents.push_back(DeserializeEvent(ej));

        std::vector<CyclicGroup> loadedGroups;
        if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
            for (const auto& gj : j["cyclicGroups"])
                loadedGroups.push_back(DeserializeGroup(gj));

        //_ Snapshot compiled-in apiWorldBossId/apiMapChestId before the merge
        // overwrites them below; restamped back in afterward (see below).
        std::unordered_map<std::string, std::string> defaultWorldBossIdByName;
        for (const auto& ev : g_Events)
            if (!ev.apiWorldBossId.empty())
                defaultWorldBossIdByName[ev.name] = ev.apiWorldBossId;

        std::unordered_map<std::string, std::string> defaultMapChestIdByName;
        for (const auto& grp : g_CyclicGroups)
            if (!grp.apiMapChestId.empty())
                defaultMapChestIdByName[grp.name] = grp.apiMapChestId;

        g_Events = MergeByKey(g_Events, loadedEvents, EventKey, resurrectMissingDefaults);
        ApplyCategoryOffsetOverrides(g_Events, savedVersion);

        g_CyclicGroups = MergeGroups(g_CyclicGroups, loadedGroups, resurrectMissingDefaults);

        //_ Restamps what MergeByKey/MergeGroups just overwrote with the loaded
        // object's blank fields - see the pair comment above for why.
        for (auto& ev : g_Events)
        {
            auto it = defaultWorldBossIdByName.find(ev.name);
            if (it != defaultWorldBossIdByName.end())
                ev.apiWorldBossId = it->second;
        }
        for (auto& grp : g_CyclicGroups)
        {
            auto it = defaultMapChestIdByName.find(grp.name);
            if (it != defaultMapChestIdByName.end())
                grp.apiMapChestId = it->second;
        }

        return true;
    }
    catch (...) { return false; }
}