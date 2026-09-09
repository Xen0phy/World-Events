//################################################################################
// events_storage.cpp
//--------------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in one
// file: "<addonDir>/events.json".
//
// On load, compiled-in defaults are merged with disk contents by key, not
// replaced outright - see MergeByKey/MergeGroups for the rule. Merge keys are
// WorldEvent::id/CyclicGroup::id/Slot::id, not name (see EventKey/GroupKey/
// SlotKey) - name is display-only. A loaded file saved before the id field
// existed has none; SlugifyName/UniqueId and the backfill pass in LoadEventsData
// give every such entry one, matching it to a compiled-in default by name where
// possible so identity survives the upgrade. The result becomes
// g_Events/g_CyclicGroups and is written back, so a first run writes exactly the
// compiled-in defaults and every run after keeps merging forward.
//
// EVENTS_DATA_VERSION (events.h) gates the merge, shared with
// events_categories.cpp via the same "data_version" key. All functions here
// swallow exceptions and return false on failure, matching settings.cpp's
// conventions.
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_categories.h"
#include "events_storage.h"
#include "localization.h"
#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;
namespace fs = std::filesystem;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicNameIdentifier / GroupNameIdentifier / SlotNameIdentifier
//--------------------------------------------------------------------------------
// Builds the event_names.csv identifier for a compiled-in Basic Event/Cyclic
// Group/Cyclic Slot, from its id (+ groupId for a Slot, since Slot::id is only
// unique within its group). Shared by DisplayName/DisplayNameEnglish below and
// LoadEventsData's legacy-customName migration, so the two stay in lockstep.
//--------------------------------------------------------------------------------
static std::string BasicNameIdentifier(const std::string& eventId)
{
    return "WE_NAME_BASIC_" + eventId;
}

static std::string GroupNameIdentifier(const std::string& groupId)
{
    return "WE_NAME_GROUP_" + groupId;
}

static std::string SlotNameIdentifier(const std::string& groupId, const std::string& slotId)
{
    return "WE_NAME_SLOT_" + groupId + "_" + slotId;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeEvent / DeserializeEvent
//--------------------------------------------------------------------------------
// (De)serializes one WorldEvent. id is the merge/identity key (see EventKey);
// DeserializeEvent leaves it empty when absent from a pre-migration file rather
// than guessing - LoadEventsData backfills it afterward, once defaults are in
// scope to match against. iconTexture/chatCode/customName are omitted when empty
// and shown is omitted when true (the default) - all four fall through cleanly
// via j.value() on load. isVarying selects varyingTimes vs period/offset (see
// WorldEvent in events.h).
//
// customName/outIsLegacyName: a file predating the customName rename has "name"
// instead. DeserializeEvent stashes that raw legacy text into customName as a
// scratch value (outIsLegacyName = true) rather than resolving it here - id
// might still be empty at this point (see EventKey backfill below), and
// resolving needs a known id to compare against WE_NAME_BASIC_<id>. See
// LoadEventsData's migration pass, which runs once ids are final.
//--------------------------------------------------------------------------------
static json SerializeEvent(const WorldEvent& ev)
{
    json j;
    j["id"]         = ev.id;
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

    if (!ev.customName.empty())
        j["customName"] = ev.customName;

    if (ev.isVarying)
        j["varyingTimes"] = ev.varyingTimes;
    else
    {
        j["period"] = ev.period;
        j["offset"] = ev.offset;
    }
    return j;
}

static WorldEvent DeserializeEvent(const json& j, bool& outIsLegacyName)
{
    WorldEvent ev{};
    ev.id          = j.value("id", std::string());
    ev.continentX  = j.value("continentX", 0.0f);
    ev.continentY  = j.value("continentY", 0.0f);
    ev.isVarying   = j.value("isVarying", false);
    ev.duration    = j.value("duration", 0);
    ev.iconTexture = j.value("iconTexture", std::string());
    ev.chatCode    = j.value("chatCode", std::string());
    ev.shown       = j.value("shown", true);

    outIsLegacyName = j.contains("name") && !j.contains("customName");
    if (j.contains("customName"))
        ev.customName = j.value("customName", std::string());
    else if (j.contains("name"))
        ev.customName = j.value("name", std::string());   //. scratch - see header comment above

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
// Converts ColorTier to/from its JSON string ("Primary"/"Secondary"/ "Tertiary");
// unrecognized strings fall back to Primary.
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
// Converts a packed ImU32 RGBA color to/from "#RRGGBBAA" hex, human-readable when
// hand-edited. Used for idleColor/customColor, which stay native ImU32 values in
// C++ and were never migrated to the float-array format ColorSet::base uses (see
// SerializeColorArray below). Falls back to `fallback` on any parse failure.
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
// ColorSet::base is written as a plain [r, g, b, a] JSON array in [0,1], the same
// layout ImGui::ColorEdit4 reads/writes directly - no packing needed (contrast
// idleColor/customColor above, which stay packed ImU32). DeserializeColorArray
// also accepts the OLD "#RRGGBBAA" hex-string shape (ColorSet::base's format
// before this migration) as a one-time read fallback; no "needs resave" flag is
// needed since SaveAllData() already runs right after LoadEventsData() on every
// AddonLoad (addon.cpp), so the very next write re-serializes through this
// function and lands on the new format automatically.
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
// (De)serializes one CyclicGroup::Slot. id is the merge/identity key, unique
// within the group (see SlotKey); left empty on deserialize for a pre-migration
// file, same deferred-backfill reasoning as SerializeEvent/DeserializeEvent
// above. customColor is presence-checked (j.contains), not defaulted, so "unset"
// round-trips exactly; chatCode/customName are omitted when empty and shown when
// true (the default), same convention as WorldEvent above. isVarying/
// varyingTimes follow the exact same convention as WorldEvent's own pair:
// isVarying always written, varyingTimes only written/read when isVarying is
// true.
//
// customName/outIsLegacyName: same scratch-value handling as
// DeserializeEvent's customName/outIsLegacyName - see that function's header
// comment. A file predating the customName rename has "name" instead.
//--------------------------------------------------------------------------------
static json SerializeSlot(const CyclicGroup::Slot& slot)
{
    json j;
    j["id"]        = slot.id;
    j["offset"]    = slot.offset;
    j["duration"]  = slot.duration;
    j["tier"]      = ColorTierToString(slot.tier);
    j["repeat"]    = slot.repeat;
    j["isVarying"] = slot.isVarying;

    if (slot.isVarying)
        j["varyingTimes"] = slot.varyingTimes;

    if (slot.customColor.has_value())
        j["customColor"] = ColorToHexString(*slot.customColor);

    if (!slot.chatCode.empty())
        j["chatCode"] = slot.chatCode;

    if (!slot.shown)
        j["shown"] = false;

    if (!slot.customName.empty())
        j["customName"] = slot.customName;

    return j;
}

static CyclicGroup::Slot DeserializeSlot(const json& j, bool& outIsLegacyName)
{
    CyclicGroup::Slot slot{};
    slot.id        = j.value("id", std::string());
    slot.offset    = j.value("offset", 0);
    slot.duration  = j.value("duration", 0);
    slot.tier      = ColorTierFromString(j.value("tier", std::string("Primary")));
    slot.repeat    = j.value("repeat", 1);
    slot.isVarying = j.value("isVarying", false);

    if (slot.isVarying)
        slot.varyingTimes = j.value("varyingTimes", std::vector<int>{});

    if (j.contains("customColor"))
        slot.customColor = HexStringToColor(j.value("customColor", std::string()), 0xFFFFFFFFu);

    slot.chatCode = j.value("chatCode", std::string());
    slot.shown    = j.value("shown", true);

    outIsLegacyName = j.contains("name") && !j.contains("customName");
    if (j.contains("customName"))
        slot.customName = j.value("customName", std::string());
    else if (j.contains("name"))
        slot.customName = j.value("name", std::string());   //. scratch - see header comment above

    return slot;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeGroup / DeserializeGroup
//--------------------------------------------------------------------------------
// (De)serializes one CyclicGroup, including its nested slots array via
// SerializeSlot/DeserializeSlot. id is the merge/identity key (see GroupKey),
// left empty on deserialize for a pre-migration file - same deferred-backfill
// reasoning as SerializeEvent/DeserializeEvent above. idleColor is presence-
// checked like Slot::customColor above; shown/customName are omitted when
// true/empty (their defaults), same convention as WorldEvent above.
//
// customName/outIsLegacyName: same scratch-value handling as
// DeserializeEvent's customName/outIsLegacyName - see that function's header
// comment. outIsLegacySlotNames is the same flag per nested slot, aligned
// index-for-index with the returned group's slots vector.
//--------------------------------------------------------------------------------
static json SerializeGroup(const CyclicGroup& grp)
{
    json j;
    j["id"]         = grp.id;
    j["continentX"] = grp.continentX;
    j["continentY"] = grp.continentY;
    j["period"]     = grp.period;
    j["colors"]     = SerializeColorArray(grp.colors.base);

    if (grp.idleColor.has_value())
        j["idleColor"] = ColorToHexString(*grp.idleColor);

    if (!grp.shown)
        j["shown"] = false;

    if (!grp.customName.empty())
        j["customName"] = grp.customName;

    json slots = json::array();
    for (const auto& slot : grp.slots)
        slots.push_back(SerializeSlot(slot));
    j["slots"] = slots;

    return j;
}

static CyclicGroup DeserializeGroup(const json& j, bool& outIsLegacyName, std::vector<bool>& outIsLegacySlotNames)
{
    CyclicGroup grp{};
    grp.id         = j.value("id", std::string());
    grp.continentX = j.value("continentX", 0.0f);
    grp.continentY = j.value("continentY", 0.0f);
    grp.period     = j.value("period", 7200);
    grp.colors     = ColorSet{ DeserializeColorArray(j.value("colors", json()), ImVec4(0.502f, 0.502f, 0.502f, 1.0f)) };   //. matches old #808080FF default

    if (j.contains("idleColor"))
        grp.idleColor = HexStringToColor(j.value("idleColor", std::string()), 0xFFFFFFFFu);

    grp.shown = j.value("shown", true);

    outIsLegacyName = j.contains("name") && !j.contains("customName");
    if (j.contains("customName"))
        grp.customName = j.value("customName", std::string());
    else if (j.contains("name"))
        grp.customName = j.value("name", std::string());   //. scratch - see header comment above

    outIsLegacySlotNames.clear();
    if (j.contains("slots") && j["slots"].is_array())
        for (const auto& sj : j["slots"])
        {
            bool slotIsLegacyName = false;
            grp.slots.push_back(DeserializeSlot(sj, slotIsLegacyName));
            outIsLegacySlotNames.push_back(slotIsLegacyName);
        }

    return grp;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeByKey
//--------------------------------------------------------------------------------
// Generic key-matched merge for top-level events/groups and for slots nested
// within a matched group. Preserves defaults' order first, then appends unmatched
// loaded entries in on-disk order. getKey must be unique within the list (see
// GroupKey/SlotKey/EventKey).
//
// A key maps to multiple loaded indices only from a pre-migration file (e.g. Jade
// Maw's old two-slot firing); every duplicate is consumed and, if
// resurrectMissingDefaults (true only pre-EVENTS_DATA_VERSION), collapsed to the
// compiled-in default. When false, an unmatched default means the user
// removed/renamed it, so a rename can't resurrect it.
//--------------------------------------------------------------------------------
template<typename T, typename KeyFn>
static std::vector<T> MergeByKey(const std::vector<T>& defaults, const std::vector<T>& loaded, KeyFn getKey, bool resurrectMissingDefaults)
{
    //_ Any index left unconsumed would leak back in as a stray unmatched entry.
    std::unordered_map<std::string, std::vector<size_t>> loadedIndicesByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndicesByKey[getKey(loaded[i])].push_back(i);

    std::vector<bool> matched(loaded.size(), false);
    std::vector<T> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndicesByKey.find(getKey(def));
        if (it != loadedIndicesByKey.end())
        {
            const std::vector<size_t>& indices = it->second;
            for (size_t idx : indices)
                matched[idx] = true;

            if (indices.size() > 1 && resurrectMissingDefaults)
            {
                result.push_back(def);
            }
            else
            {
                result.push_back(loaded[indices.back()]);
            }
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
// GroupKey / SlotKey / EventKey
//--------------------------------------------------------------------------------
// Merge keys for MergeGroups/MergeByKey. Groups, events, and slots all key on id
// - for slots this means unique WITHIN the group, not globally (see
// CyclicGroup::Slot::id, events.h). name is display-only and can change (user
// rename, localization) without breaking the merge match. MergeByKey collapses
// same-key duplicates from a pre-migration file back down to the single compiled-
// in default (see MergeByKey above), not the old multi-entry shape.
//--------------------------------------------------------------------------------
static std::string GroupKey(const CyclicGroup& g) { return g.id; }
static std::string SlotKey(const CyclicGroup::Slot& s) { return s.id; }
static std::string EventKey(const WorldEvent& e) { return e.id; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeGroups
//--------------------------------------------------------------------------------
// MergeByKey for CyclicGroup, plus one extra pass: even when a group matches by
// key and the loaded version wins overall, its slots are still merged one level
// deeper via MergeByKey, so a new slot added to that group in a newer build still
// appears instead of being replaced wholesale by the loaded group object.
// resurrectMissingDefaults is forwarded to both levels, so a slot the user
// deleted from an otherwise-matched group follows the same resurrect-or-drop rule
// as the group itself.
//--------------------------------------------------------------------------------
static std::vector<CyclicGroup> MergeGroups(const std::vector<CyclicGroup>& defaults, const std::vector<CyclicGroup>& loaded, bool resurrectMissingDefaults)
{
    //_ Same duplicate-key handling as MergeByKey - see that function above.
    std::unordered_map<std::string, std::vector<size_t>> loadedIndicesByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndicesByKey[GroupKey(loaded[i])].push_back(i);

    std::vector<bool> matched(loaded.size(), false);
    std::vector<CyclicGroup> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndicesByKey.find(GroupKey(def));
        if (it != loadedIndicesByKey.end())
        {
            const std::vector<size_t>& indices = it->second;
            for (size_t idx : indices)
                matched[idx] = true;

            if (indices.size() > 1 && resurrectMissingDefaults)
            {
                //_ Pre-migration duplicate - takes compiled-in group wholesale.
                result.push_back(def);
            }
            else
            {
                CyclicGroup merged = loaded[indices.back()];
                merged.slots = MergeByKey(def.slots, loaded[indices.back()].slots, SlotKey, resurrectMissingDefaults);
                result.push_back(merged);
            }
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
// ApplyCategoryOffsetOverrides / ApplyCategoryDurationOverrides
//--------------------------------------------------------------------------------
// Implement CategoryDefaultMember::offset/duration (events_categories.h). Read
// straight from g_DefaultBasicCategories - the same compiled-in list
// events_basic.cpp already maintains for category placement - so a schedule fix
// is a one-line edit next to that member's category entry, no separate table to
// keep in sync. Same version gate as ForceCategoryMembership
// (events_categories.cpp): runs once while the saved file predates
// EVENTS_DATA_VERSION. Whenever either override corrects bad compiled-in data,
// EVENTS_DATA_VERSION (events.h) must be bumped too, or an already-current file
// never re-enters this gate.
//--------------------------------------------------------------------------------
static void ApplyCategoryOffsetOverrides(std::vector<WorldEvent>& events, int64_t savedVersion)
{
    if (savedVersion >= EVENTS_DATA_VERSION) return;

    for (const auto& def : g_DefaultBasicCategories)
        for (const auto& m : def.members)
            if (m.offset.has_value())
                for (auto& ev : events)
                    if (ev.id == m.id)
                        ev.offset = *m.offset;
}

static void ApplyCategoryDurationOverrides(std::vector<WorldEvent>& events, int64_t savedVersion)
{
    if (savedVersion >= EVENTS_DATA_VERSION) return;

    for (const auto& def : g_DefaultBasicCategories)
        for (const auto& m : def.members)
            if (m.duration.has_value())
                for (auto& ev : events)
                    if (ev.id == m.id)
                        ev.duration = *m.duration;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ApplySlotOverrides
//--------------------------------------------------------------------------------
// Implements SlotOverride (events.h) - the Slot-granularity counterpart to
// ApplyCategoryOffsetOverrides/ApplyCategoryDurationOverrides above, for
// corrections inside a CyclicGroup's slots, not at the group level. Same version
// gate: runs once, only while the saved file predates EVENTS_DATA_VERSION.
//--------------------------------------------------------------------------------
static void ApplySlotOverrides(std::vector<CyclicGroup>& groups, int64_t savedVersion)
{
    if (savedVersion >= EVENTS_DATA_VERSION) return;

    for (const auto& ov : g_SlotOverrides)
        for (auto& grp : groups)
            if (grp.id == ov.groupId)
                for (auto& slot : grp.slots)
                    if (slot.id == ov.slotId)
                    {
                        if (ov.offset.has_value())
                            slot.offset = *ov.offset;
                        if (ov.duration.has_value())
                            slot.duration = *ov.duration;
                    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SlugifyName
//--------------------------------------------------------------------------------
// Lowercases, drops apostrophes, and collapses every other non-alphanumeric run
// to a single underscore (leading/trailing underscores stripped). Same scheme
// used by hand for the compiled-in ids (events_basic.cpp/events_cyclic.cpp).
// Migration fallback only, for a loaded WorldEvent/CyclicGroup name that doesn't
// match any compiled-in default's DisplayNameEnglish - see LoadEventsData.
//--------------------------------------------------------------------------------
static std::string SlugifyName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    bool pendingUnderscore = false;

    for (char c : name)
    {
        if (c == '\'') continue;

        char lower = (char)std::tolower((unsigned char)c);
        bool isAlnum = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (isAlnum)
        {
            if (pendingUnderscore && !out.empty())
                out += '_';
            pendingUnderscore = false;
            out += lower;
        }
        else
        {
            pendingUnderscore = true;
        }
    }
    return out;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UniqueId
//--------------------------------------------------------------------------------
// Returns `candidate`, or "candidate_2"/"_3"/... if it's already in `used`, and
// reserves whichever id it returns. Only matters for SlugifyName fallbacks - two
// differently-punctuated names can slugify to the same string.
//--------------------------------------------------------------------------------
static std::string UniqueId(const std::string& candidate, std::unordered_set<std::string>& used)
{
    if (used.insert(candidate).second)
        return candidate;

    for (int n = 2; ; n++)
    {
        std::string next = candidate + "_" + std::to_string(n);
        if (used.insert(next).second)
            return next;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveEventsData / LoadEventsData
//--------------------------------------------------------------------------------
// SaveEventsData serializes g_Events/g_CyclicGroups straight to events.json.
// LoadEventsData backfills a missing id (pre-migration file - see
// SlugifyName/UniqueId above) before anything else runs, then merges from disk
// via MergeByKey/MergeGroups, using resurrectMissingDefaults = (saved
// data_version < EVENTS_DATA_VERSION), then restamps apiWorldBossId/doneGroup/
// apiMapChestId from the compiled-in defaults by id, since those cross-reference
// fields are never read from or written to the file. A missing file isn't an
// error - g_Events/g_CyclicGroups are simply left at their compiled-in defaults;
// the caller (addon.cpp) is expected to call SaveEventsData right after so the
// file exists from then on.
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

//_ Compiled-in roster snapshot, captured once - see ResetEventsToDefaults.
static std::vector<WorldEvent>  s_compiledDefaultEvents;
static std::vector<CyclicGroup> s_compiledDefaultGroups;
static bool                     s_compiledDefaultsCaptured = false;

bool LoadEventsData(const std::string& addonDir)
{
    if (!s_compiledDefaultsCaptured)
    {
        s_compiledDefaultEvents    = g_Events;
        s_compiledDefaultGroups    = g_CyclicGroups;
        s_compiledDefaultsCaptured = true;
    }

    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false;   //. no file yet, keep defaults

        json j = json::parse(file);

        int64_t savedVersion = j.value("data_version", (int64_t)0);
        bool resurrectMissingDefaults = savedVersion < EVENTS_DATA_VERSION;

        std::vector<WorldEvent> loadedEvents;
        std::vector<bool>       loadedEventIsLegacyName;   //. see DeserializeEvent's header comment
        if (j.contains("events") && j["events"].is_array())
            for (const auto& ej : j["events"])
            {
                bool isLegacyName = false;
                loadedEvents.push_back(DeserializeEvent(ej, isLegacyName));
                loadedEventIsLegacyName.push_back(isLegacyName);
            }

        std::vector<CyclicGroup> loadedGroups;
        std::vector<bool>       loadedGroupIsLegacyName;      //. see DeserializeGroup's header comment
        std::vector<std::vector<bool>> loadedSlotIsLegacyName; //. aligned with each group's slots
        if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
            for (const auto& gj : j["cyclicGroups"])
            {
                bool isLegacyName = false;
                std::vector<bool> slotLegacyFlags;
                loadedGroups.push_back(DeserializeGroup(gj, isLegacyName, slotLegacyFlags));
                loadedGroupIsLegacyName.push_back(isLegacyName);
                loadedSlotIsLegacyName.push_back(std::move(slotLegacyFlags));
            }

        std::unordered_set<std::string> usedEventIds;
        for (const auto& d : g_Events)
            usedEventIds.insert(d.id);

        for (auto& ev : loadedEvents)
        {
            //_ Pre-id file: ev.customName is still the raw legacy "name" text here (DeserializeEvent's scratch use).
            if (!ev.id.empty()) { usedEventIds.insert(ev.id); continue; }

            const WorldEvent* def = nullptr;
            for (const auto& d : g_Events)
                if (DisplayNameEnglish(d) == ev.customName) { def = &d; break; }

            ev.id = def ? def->id : UniqueId(SlugifyName(ev.customName), usedEventIds);
        }

        //_ Resolves the scratch value DeserializeEvent left in customName for every legacy-format row, now that every id above is final - see the Migration section this implements (localization-handoff.md).
        for (size_t i = 0; i < loadedEvents.size(); i++)
        {
            if (!loadedEventIsLegacyName[i]) continue;   //. already customName-format on disk

            if (loadedEvents[i].customName == TrEnglish(BasicNameIdentifier(loadedEvents[i].id).c_str()))
                loadedEvents[i].customName.clear();   //. matches the compiled default - starts localizing going forward
            //. else: keep the literal legacy text - the user's rename survives the upgrade
        }

        std::unordered_set<std::string> usedGroupIds;
        for (const auto& d : g_CyclicGroups)
            usedGroupIds.insert(d.id);

        for (auto& grp : loadedGroups)
        {
            //_ Pre-id file: grp.customName is still the raw legacy "name" text here (DeserializeGroup's scratch use).
            const CyclicGroup* defGroup = nullptr;
            for (const auto& d : g_CyclicGroups)
                if (DisplayNameEnglish(d) == grp.customName) { defGroup = &d; break; }

            if (grp.id.empty())
                grp.id = defGroup ? defGroup->id : UniqueId(SlugifyName(grp.customName), usedGroupIds);
            else
                usedGroupIds.insert(grp.id);

            //_ Slot ids only need to be unique within this group.
            std::unordered_set<std::string> usedSlotIds;
            if (defGroup)
                for (const auto& s : defGroup->slots)
                    usedSlotIds.insert(s.id);

            for (auto& slot : grp.slots)
            {
                if (!slot.id.empty()) { usedSlotIds.insert(slot.id); continue; }

                const CyclicGroup::Slot* defSlot = nullptr;
                if (defGroup)
                    for (const auto& s : defGroup->slots)
                        if (DisplayNameEnglish(s, defGroup->id) == slot.customName) { defSlot = &s; break; }

                slot.id = defSlot ? defSlot->id : UniqueId(SlugifyName(slot.customName), usedSlotIds);
            }
        }

        //_ Resolves the scratch value DeserializeGroup/DeserializeSlot left in customName for every legacy-format row, now that every id above is final - see the Migration section this implements (localization-handoff.md).
        for (size_t i = 0; i < loadedGroups.size(); i++)
        {
            if (loadedGroupIsLegacyName[i])
            {
                if (loadedGroups[i].customName == TrEnglish(GroupNameIdentifier(loadedGroups[i].id).c_str()))
                    loadedGroups[i].customName.clear();   //. matches the compiled default - starts localizing going forward
                //. else: keep the literal legacy text - the user's rename survives the upgrade
            }

            for (size_t s = 0; s < loadedGroups[i].slots.size(); s++)
            {
                if (s >= loadedSlotIsLegacyName[i].size() || !loadedSlotIsLegacyName[i][s]) continue;   //. already customName-format on disk

                CyclicGroup::Slot& slot = loadedGroups[i].slots[s];
                if (slot.customName == TrEnglish(SlotNameIdentifier(loadedGroups[i].id, slot.id).c_str()))
                    slot.customName.clear();
            }
        }

        //_ Snapshotted before the merge overwrites them - restamped below.
        std::unordered_map<std::string, std::string> defaultWorldBossIdById;
        std::unordered_map<std::string, std::string> defaultDoneGroupById;
        for (const auto& ev : g_Events)
        {
            if (!ev.apiWorldBossId.empty())
                defaultWorldBossIdById[ev.id] = ev.apiWorldBossId;
            if (!ev.doneGroup.empty())
                defaultDoneGroupById[ev.id] = ev.doneGroup;
        }

        std::unordered_map<std::string, std::string> defaultMapChestIdById;
        for (const auto& grp : g_CyclicGroups)
            if (!grp.apiMapChestId.empty())
                defaultMapChestIdById[grp.id] = grp.apiMapChestId;

        g_Events = MergeByKey(g_Events, loadedEvents, EventKey, resurrectMissingDefaults);
        ApplyCategoryOffsetOverrides(g_Events, savedVersion);
        ApplyCategoryDurationOverrides(g_Events, savedVersion);

        g_CyclicGroups = MergeGroups(g_CyclicGroups, loadedGroups, resurrectMissingDefaults);
        ApplySlotOverrides(g_CyclicGroups, savedVersion);

        //_ Restamps the fields the merge above just overwrote - see snapshot.
        for (auto& ev : g_Events)
        {
            auto it = defaultWorldBossIdById.find(ev.id);
            if (it != defaultWorldBossIdById.end())
                ev.apiWorldBossId = it->second;

            auto git = defaultDoneGroupById.find(ev.id);
            if (git != defaultDoneGroupById.end())
                ev.doneGroup = git->second;
        }
        for (auto& grp : g_CyclicGroups)
        {
            auto it = defaultMapChestIdById.find(grp.id);
            if (it != defaultMapChestIdById.end())
                grp.apiMapChestId = it->second;
        }

        return true;
    }
    catch (...) { return false; }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetEventsToDefaults   (see: events_storage.h)
//--------------------------------------------------------------------------------
void ResetEventsToDefaults()
{
    if (!s_compiledDefaultsCaptured) return;   //. LoadEventsData never ran

    g_Events       = s_compiledDefaultEvents;
    g_CyclicGroups = s_compiledDefaultGroups;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetDefaultEvent / GetDefaultCyclicGroup / GetDefaultCyclicSlot
//--------------------------------------------------------------------------------
// Plain linear scan over s_compiledDefaultEvents/s_compiledDefaultGroups - small,
// options-panel-only lookups, not worth an index. Keyed on id, not name (see
// events_storage.h) - a renamed row still matches its compiled-in default.
//--------------------------------------------------------------------------------
const WorldEvent* GetDefaultEvent(const std::string& id)
{
    if (!s_compiledDefaultsCaptured) return nullptr;

    for (const auto& ev : s_compiledDefaultEvents)
        if (ev.id == id)
            return &ev;
    return nullptr;
}

const CyclicGroup* GetDefaultCyclicGroup(const std::string& id)
{
    if (!s_compiledDefaultsCaptured) return nullptr;

    for (const auto& grp : s_compiledDefaultGroups)
        if (grp.id == id)
            return &grp;
    return nullptr;
}

const CyclicGroup::Slot* GetDefaultCyclicSlot(const std::string& groupId, const std::string& slotId)
{
    const CyclicGroup* grp = GetDefaultCyclicGroup(groupId);
    if (!grp) return nullptr;

    for (const auto& slot : grp->slots)
        if (slot.id == slotId)
            return &slot;
    return nullptr;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DisplayName / DisplayNameEnglish   (see: events_storage.h)
//--------------------------------------------------------------------------------
const char* DisplayName(const WorldEvent& ev)
{
    if (!ev.customName.empty()) return ev.customName.c_str();
    if (GetDefaultEvent(ev.id)) return Tr(BasicNameIdentifier(ev.id).c_str());
    return Tr("WE_UNNAMED");
}

const char* DisplayNameEnglish(const WorldEvent& ev)
{
    if (!ev.customName.empty()) return ev.customName.c_str();
    if (GetDefaultEvent(ev.id)) return TrEnglish(BasicNameIdentifier(ev.id).c_str());
    return TrEnglish("WE_UNNAMED");
}

const char* DisplayName(const CyclicGroup& grp)
{
    if (!grp.customName.empty()) return grp.customName.c_str();
    if (GetDefaultCyclicGroup(grp.id)) return Tr(GroupNameIdentifier(grp.id).c_str());
    return Tr("WE_UNNAMED");
}

const char* DisplayNameEnglish(const CyclicGroup& grp)
{
    if (!grp.customName.empty()) return grp.customName.c_str();
    if (GetDefaultCyclicGroup(grp.id)) return TrEnglish(GroupNameIdentifier(grp.id).c_str());
    return TrEnglish("WE_UNNAMED");
}

const char* DisplayName(const CyclicGroup::Slot& slot, const std::string& groupId)
{
    if (!slot.customName.empty()) return slot.customName.c_str();
    if (GetDefaultCyclicSlot(groupId, slot.id)) return Tr(SlotNameIdentifier(groupId, slot.id).c_str());
    return Tr("WE_UNNAMED");
}

const char* DisplayNameEnglish(const CyclicGroup::Slot& slot, const std::string& groupId)
{
    if (!slot.customName.empty()) return slot.customName.c_str();
    if (GetDefaultCyclicSlot(groupId, slot.id)) return TrEnglish(SlotNameIdentifier(groupId, slot.id).c_str());
    return TrEnglish("WE_UNNAMED");
}