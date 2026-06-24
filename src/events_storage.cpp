// events_storage.cpp
// JSON persistence for g_CyclicGroups and g_Events, both stored together in
// one file: "<addonDir>/events.json".
//
// File shape:
//   {
//     "data_version": 1,
//     "events": [ {...WorldEvent...}, ... ],
//     "cyclicGroups": [ {...CyclicGroup, with nested "slots": [...]...}, ... ]
//   }
//
// On load, the compiled-in defaults (g_Events / g_CyclicGroups as built by
// events.cpp / cyclic.cpp) are MERGED with whatever's on disk, matched by
// KEY — not by array position, since inserting a new group/event in the
// middle of the compiled-in list would otherwise shift every later index
// and corrupt the match. Groups and events are keyed by name alone (no
// duplicates exist among them as of this writing); SLOTS are keyed by
// name+offset together, since slot names CAN legitimately repeat within a
// single group — e.g. Dry Top has two slots both named "Crash Site" at
// different offsets, a real repeated boss fight, not a typo. An earlier
// version of this file keyed slots by name alone, which silently merged
// Dry Top's two same-named slots into each other and then duplicated them
// further on every subsequent load/save cycle — see SlotKey() below.
//
//   - Key exists in both       -> keep the JSON's version (preserves any
//                                 user customization — color overrides,
//                                 edited offsets, etc.). Slots within a
//                                 matched group are merged the same way,
//                                 one level deeper, by slot key.
//   - Key only in defaults     -> new content shipped in this build; add
//                                 it to the merged result.
//   - Key only in the JSON     -> NOT dropped. We can't tell a renamed/
//                                 removed compiled-in entry apart from
//                                 something the user created entirely
//                                 themselves (once group/slot editing UI
//                                 exists), so orphaned entries are kept.
//
// The merged result is what g_Events / g_CyclicGroups actually end up
// holding, and is also what gets written back to disk afterward — so a
// first run (no file yet) writes out exactly the compiled-in defaults,
// and every run after that keeps merging forward.
//
// All functions swallow exceptions and return false on failure, matching
// the conventions in settings.cpp.

#include "events.h"
#include "cyclic.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <deque>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Bump this when the SHAPE of the on-disk data changes in a way old files
// can't just fall through defaults for (e.g. a field is removed/renamed,
// not just added). Adding a new optional field with a sensible j.value()
// default does NOT require a bump — old files keep loading fine, the new
// field just falls back to its default until the user sets it.
static constexpr int EVENTS_DATA_VERSION = 1;

// ---------------------------------------------------------------------------
// WorldEvent (de)serialization
// ---------------------------------------------------------------------------
// `name` is stored as a real C++ string in JSON but WorldEvent::name is a
// const char* (string-literal-backed in events.cpp). Deserialized entries
// need their own persistent storage for the string — see InternName()
// below, used by WorldEvent, CyclicGroup, and CyclicGroup::Slot
// deserialization alike.
// ---------------------------------------------------------------------------

// Deserialized name/event-name strings need to outlive the json object they
// came from, but WorldEvent/CyclicGroup/Slot only store a const char*, not
// a std::string. This keeps every deserialized name string alive for the
// lifetime of the program (same lifetime as g_Events/g_CyclicGroups
// themselves), avoiding a dangling pointer the moment the loader's local
// json/string temporaries go out of scope.
//
// IMPORTANT: this MUST be std::deque, not std::vector. push_back on a
// vector can reallocate its entire backing buffer once capacity is
// exceeded, which silently invalidates every c_str() pointer handed out by
// earlier calls — turning every name already in g_Events/g_CyclicGroups
// into a dangling pointer the moment a later InternName() call triggers a
// resize. std::deque never relocates existing elements on push_back, so a
// pointer returned by an earlier call stays valid no matter how many more
// names get interned afterward.
static std::deque<std::string> s_NameStorage;
static const char* InternName(const std::string& s)
{
    s_NameStorage.push_back(s);
    return s_NameStorage.back().c_str();
}

static json SerializeEvent(const WorldEvent& ev)
{
    json j;
    j["name"]       = ev.name;
    j["continentX"] = ev.continentX;
    j["continentY"] = ev.continentY;
    j["isVarying"]  = ev.isVarying;
    j["duration"]   = ev.duration;

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
    ev.name       = InternName(j.value("name", std::string("Unnamed Event")));
    ev.continentX = j.value("continentX", 0.0f);
    ev.continentY = j.value("continentY", 0.0f);
    ev.isVarying  = j.value("isVarying", false);
    ev.duration   = j.value("duration", 0);

    if (ev.isVarying)
        ev.varyingTimes = j.value("varyingTimes", std::vector<int>{});
    else
    {
        ev.period = j.value("period", 7200);
        ev.offset = j.value("offset", 0);
    }
    return ev;
}

// ---------------------------------------------------------------------------
// CyclicGroup::Slot (de)serialization
// ---------------------------------------------------------------------------
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

// Colors are stored as "#RRGGBBAA" hex strings in the JSON — readable when
// hand-edited — but stay plain ImU32/unsigned int everywhere in C++.
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
    // omitted entirely when unset — j.value() on load falls through cleanly

    return j;
}

static CyclicGroup::Slot DeserializeSlot(const json& j)
{
    CyclicGroup::Slot slot{};
    slot.name     = InternName(j.value("name", std::string("Unnamed Event")));
    slot.offset   = j.value("offset", 0);
    slot.duration = j.value("duration", 0);
    slot.tier     = ColorTierFromString(j.value("tier", std::string("Primary")));
    slot.repeat   = j.value("repeat", 1);

    if (j.contains("customColor"))
        slot.customColor = HexStringToColor(j.value("customColor", std::string()), 0xFFFFFFFFu);

    return slot;
}

// ---------------------------------------------------------------------------
// CyclicGroup (de)serialization
// ---------------------------------------------------------------------------
static json SerializeGroup(const CyclicGroup& grp)
{
    json j;
    j["name"]       = grp.name;
    j["continentX"] = grp.continentX;
    j["continentY"] = grp.continentY;
    j["period"]     = grp.period;
    j["colors"]     = ColorToHexString(grp.colors.base);

    if (grp.idleColor.has_value())
        j["idleColor"] = ColorToHexString(*grp.idleColor);

    json slots = json::array();
    for (const auto& slot : grp.slots)
        slots.push_back(SerializeSlot(slot));
    j["slots"] = slots;

    return j;
}

static CyclicGroup DeserializeGroup(const json& j)
{
    CyclicGroup grp{};
    grp.name       = InternName(j.value("name", std::string("Unnamed Group")));
    grp.continentX = j.value("continentX", 0.0f);
    grp.continentY = j.value("continentY", 0.0f);
    grp.period     = j.value("period", 7200);
    grp.colors     = ColorSet{ HexStringToColor(j.value("colors", std::string("#808080FF")), 0x808080FFu) };

    if (j.contains("idleColor"))
        grp.idleColor = HexStringToColor(j.value("idleColor", std::string()), 0xFFFFFFFFu);

    if (j.contains("slots") && j["slots"].is_array())
        for (const auto& sj : j["slots"])
            grp.slots.push_back(DeserializeSlot(sj));

    return grp;
}

// ---------------------------------------------------------------------------
// MergeByKey
// ---------------------------------------------------------------------------
// Generic merge used for both top-level (events/groups) and nested (slots
// within a matched group) merging:
//   - key present in both  -> loaded (disk) version wins
//   - key only in defaults -> append (new compiled-in content)
//   - key only in loaded   -> append (orphaned/user-created, kept as-is)
// Preserves defaults' relative order first, then appends anything in
// loaded that wasn't matched, in the order it appeared on disk.
//
// IMPORTANT: the key function must produce a value that's actually unique
// within the list, or entries silently collide and corrupt each other on
// merge (overwriting one another in the lookup map, double-counting, or
// getting duplicated across repeated load/save cycles). Plain name is NOT
// safe for this on its own — e.g. Dry Top has two slots both named
// "Crash Site" (different offsets, a real and intentional repeat boss
// fight), and a name-only key silently collapsed/duplicated them across
// merges. getKey should combine name with whatever else disambiguates
// otherwise-identical-looking entries — see callers below for the actual
// key used for groups/events vs slots.
// ---------------------------------------------------------------------------
template<typename T, typename KeyFn>
static std::vector<T> MergeByKey(const std::vector<T>& defaults, const std::vector<T>& loaded, KeyFn getKey)
{
    std::unordered_map<std::string, size_t> loadedIndexByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByKey[getKey(loaded[i])] = i; // last entry with a given key wins if the
                                                  // caller's key isn't actually unique —
                                                  // see the IMPORTANT note above.

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
        else
        {
            result.push_back(def);
        }
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

// Key helpers. Plain name is fine for top-level groups/events today (no
// duplicates exist in cyclic.cpp/events.cpp as of this writing), but slots
// within a single group CAN legitimately share a name — e.g. Dry Top has
// two slots both named "Crash Site" at different offsets, a real repeat
// occurrence, not a typo. Composite name+offset keys are used everywhere
// an offset exists, so a future name collision (a user renaming things, or
// a slot/group genuinely sharing a name elsewhere) can't silently corrupt
// the merge the way a name-only key did before this fix.
static std::string GroupKey(const CyclicGroup& g) { return g.name; }
static std::string SlotKey(const CyclicGroup::Slot& s) { return std::string(s.name) + "|" + std::to_string(s.offset); }
static std::string EventKey(const WorldEvent& e) { return e.name; }

// CyclicGroups need an extra pass: even when a group itself matches by key
// (so the loaded group "wins" overall), its SLOTS still need to be merged
// the same way one level deeper — otherwise a new slot added to that group
// in a newer build would never appear, since the whole loaded group object
// would simply replace the compiled-in one wholesale.
static std::vector<CyclicGroup> MergeGroups(const std::vector<CyclicGroup>& defaults, const std::vector<CyclicGroup>& loaded)
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
            CyclicGroup merged = loaded[it->second]; // loaded group wins at the top level...
            // ...but its slot list is merged with the compiled-in slot list,
            // so newly-added slots in `def` still show up even though the
            // rest of the group's fields come from the loaded version.
            merged.slots = MergeByKey(def.slots, loaded[it->second].slots, SlotKey);
            result.push_back(merged);
            matched[it->second] = true;
        }
        else
        {
            result.push_back(def);
        }
    }

    for (size_t i = 0; i < loaded.size(); i++)
        if (!matched[i])
            result.push_back(loaded[i]);

    return result;
}

// ---------------------------------------------------------------------------
// SaveEventsData
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// LoadEventsData
// ---------------------------------------------------------------------------
// Merges the compiled-in g_Events/g_CyclicGroups (as already populated by
// events.cpp/cyclic.cpp before this is called) with whatever's saved on
// disk, by name, per the rules described at the top of this file. The
// merged result REPLACES g_Events/g_CyclicGroups in place.
//
// Missing file -> not an error: g_Events/g_CyclicGroups are simply left as
// the compiled-in defaults, and the caller (addon.cpp) is expected to call
// SaveEventsData afterward so the file exists from then on.
// ---------------------------------------------------------------------------
bool LoadEventsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false; // no file yet — keep compiled-in defaults

        json j = json::parse(file);

        std::vector<WorldEvent> loadedEvents;
        if (j.contains("events") && j["events"].is_array())
            for (const auto& ej : j["events"])
                loadedEvents.push_back(DeserializeEvent(ej));

        std::vector<CyclicGroup> loadedGroups;
        if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
            for (const auto& gj : j["cyclicGroups"])
                loadedGroups.push_back(DeserializeGroup(gj));

        g_Events = MergeByKey(g_Events, loadedEvents, EventKey);

        g_CyclicGroups = MergeGroups(g_CyclicGroups, loadedGroups);

        return true;
    }
    catch (...) { return false; }
}
