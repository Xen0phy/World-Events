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

using json = nlohmann::json;
namespace fs = std::filesystem;

// A date, as an int (YYYYMMDD), bumped whenever EITHER of these change:
//   - the on-disk SHAPE changes in a way old files can't just fall through
//     defaults for (a field is removed/renamed, not just added — adding a
//     new optional field with a sensible j.value() default does NOT need
//     a bump, since old files keep loading fine and the new field just
//     falls back to its default until the user sets it)
//   - the COMPILED-IN CONTENT changes (a group/event/slot was added,
//     removed, or renamed in cyclic.cpp/events.cpp)
//
// This drives the merge behavior in LoadEventsData below: if the saved
// file's version already matches this constant, the file is known to be
// fully current with the compiled-in defaults, so a name present in the
// defaults but missing from the file is treated as something the USER
// removed/renamed — it is NOT resurrected. Resurrection (treating a
// missing default as new shipped content) only happens when this
// constant is genuinely newer than what's saved, i.e. an actual new
// build with actual new/changed content.
//
// Without this check, renaming something to collide with another
// existing name would cause the old name to come back from the compiled
// defaults on the very next load (looking, to the merge, identical to
// "a new build added this back") while the renamed duplicate also
// persisted — a real bug found and fixed this session.
static constexpr int EVENTS_DATA_VERSION = 20260626;

// ---------------------------------------------------------------------------
// WorldEvent (de)serialization
// ---------------------------------------------------------------------------

static json SerializeEvent(const WorldEvent& ev)
{
    json j;
    j["name"]       = ev.name;
    j["continentX"] = ev.continentX;
    j["continentY"] = ev.continentY;
    j["isVarying"]  = ev.isVarying;
    j["duration"]   = ev.duration;

    if (!ev.iconTexture.empty())
        j["iconTexture"] = ev.iconTexture; // omitted entirely when empty, matching customColor/idleColor's convention elsewhere in this file

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
    slot.name     = j.value("name", std::string("Unnamed Event"));
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
    grp.name       = j.value("name", std::string("Unnamed Group"));
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
//   - key only in defaults -> append IF resurrectMissingDefaults is true
//                              (new compiled-in content from a newer
//                              build); otherwise DROPPED — the file is
//                              already current with these defaults, so a
//                              missing name means the user removed or
//                              renamed it, not that it's new content.
//   - key only in loaded   -> append (orphaned/user-created, kept as-is)
// Preserves defaults' relative order first, then appends anything in
// loaded that wasn't matched, in the order it appeared on disk.
//
// resurrectMissingDefaults should be true only when the file's saved
// data_version is OLDER than the compiled-in EVENTS_DATA_VERSION — see
// LoadEventsData. When the versions match, the file is known fully
// current, and treating an unmatched default as "new content" instead of
// "the user changed it" is exactly what caused a real bug this session:
// renaming something to collide with another existing name made the old
// name reappear from the compiled defaults on the next load, alongside
// the renamed duplicate, since the merge couldn't tell a rename apart
// from a brand new build adding content back.
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
static std::vector<T> MergeByKey(const std::vector<T>& defaults, const std::vector<T>& loaded, KeyFn getKey, bool resurrectMissingDefaults)
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
        else if (resurrectMissingDefaults)
        {
            result.push_back(def);
        }
        // else: dropped — the file is current, this default's absence
        // from it means the user removed/renamed it, not new content.
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
static std::string SlotKey(const CyclicGroup::Slot& s) { return s.name + "|" + std::to_string(s.offset); }
static std::string EventKey(const WorldEvent& e) { return e.name; }

// CyclicGroups need an extra pass: even when a group itself matches by key
// (so the loaded group "wins" overall), its SLOTS still need to be merged
// the same way one level deeper — otherwise a new slot added to that group
// in a newer build would never appear, since the whole loaded group object
// would simply replace the compiled-in one wholesale.
//
// resurrectMissingDefaults is forwarded to BOTH levels: a group missing
// from the file is only re-added when the file predates this build's
// content (see MergeByKey's comment above for why), and a SLOT missing
// from an otherwise-matched group's loaded slot list follows the exact
// same rule — a slot the user deleted from an existing group shouldn't
// reappear just because the group itself still exists.
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
            CyclicGroup merged = loaded[it->second]; // loaded group wins at the top level...
            // ...but its slot list is merged with the compiled-in slot list,
            // so newly-added slots in `def` still show up even though the
            // rest of the group's fields come from the loaded version.
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
// The file's saved "data_version" is compared against the compiled-in
// EVENTS_DATA_VERSION: if the file is already current (saved >= compiled,
// the normal case — "<" handles a build downgrade gracefully too, see
// below), a compiled-in default missing from the file is treated as
// something the user removed/renamed, NOT new content, and is dropped
// rather than resurrected. Only a genuinely older file (saved <
// compiled — an actual upgrade to a build with new/changed content) gets
// the resurrection behavior. This is what fixes the rename-collision bug:
// renaming something to match an existing name no longer brings the old
// name back from the compiled defaults on the next load, since the file
// is (almost always) already current.
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

        int savedVersion = j.value("data_version", 0);
        bool resurrectMissingDefaults = savedVersion < EVENTS_DATA_VERSION;

        std::vector<WorldEvent> loadedEvents;
        if (j.contains("events") && j["events"].is_array())
            for (const auto& ej : j["events"])
                loadedEvents.push_back(DeserializeEvent(ej));

        std::vector<CyclicGroup> loadedGroups;
        if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
            for (const auto& gj : j["cyclicGroups"])
                loadedGroups.push_back(DeserializeGroup(gj));

        g_Events = MergeByKey(g_Events, loadedEvents, EventKey, resurrectMissingDefaults);

        g_CyclicGroups = MergeGroups(g_CyclicGroups, loadedGroups, resurrectMissingDefaults);

        return true;
    }
    catch (...) { return false; }
}
