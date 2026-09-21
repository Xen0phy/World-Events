//################################################################################
// events_storage.cpp
//--------------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in one
// file: "<addonDir>/events.json".
//
// On load, compiled-in defaults are merged with disk contents by key, not
// replaced outright - see MergeByKey/MergeGroups for the rule. Merge keys are
// WorldEvent::id/CyclicGroup::id/Slot::id, not name (see EventKey/GroupKey/
// SlotKey) - name is display-only. Every entry on disk already has an id: a pre-
// id-migration file is deleted before it ever reaches here (see
// WipeLegacyEventsFile, addon.cpp), and the options panel's "+" button assigns
// one up front for anything created since (see options_events.cpp). The id
// backfill below only fires for a save written by a build that predates that
// assignment.
//
// EVENTS_DATA_VERSION (events.h) gates one-time corrections to compiled-in data
// (ApplyCategoryOffsetOverrides/ApplyCategoryDurationOverrides/
// ApplySlotOverrides below), shared with events_categories.cpp via the same
// "data_version" key. All functions here swallow exceptions and return false on
// failure, matching settings.cpp's conventions.
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
// unique within its group). Used by DisplayName/DisplayNameEnglish below.
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
// IsAbandonedEntry
//--------------------------------------------------------------------------------
// True for a WorldEvent/CyclicGroup/Slot with a blank (or whitespace-only) name
// that also doesn't resolve to a compiled-in default - the state a "+"-created
// entry is left in if the player never gets around to naming it (see
// RequestBasicEventNameEdit, options_events_rows.h) and closes/unloads anyway.
// SaveEventsData drops these instead of writing them to events.json as a
// permanent "(unnamed)" row; a real stock default with an unedited (blank)
// customName still resolves via its id and is kept.
//--------------------------------------------------------------------------------
static bool IsAbandonedEntry(const std::string& customName, bool hasDefault)
{
    bool blank = customName.find_first_not_of(" \t") == std::string::npos;
    return blank && !hasDefault;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeEvent / DeserializeEvent
//--------------------------------------------------------------------------------
// (De)serializes one WorldEvent. id is the merge/identity key (see EventKey),
// assigned up front when the entry is created (see SlugifyName/UniqueId,
// events_storage.h) - LoadEventsData's id backfill only matters for a save
// written before that assignment existed. iconTexture/chatCode/customName are
// omitted when empty, shown is omitted when true (the default) - all four fall
// through via j.value() on load. isVarying selects varyingTimes vs period/offset
// (see WorldEvent in events.h).
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

    //_ screenX/Y are meaningless (and left at their default) while unset, so skip them too.
    if (ev.fixedToScreen)
    {
        j["fixedToScreen"] = true;
        j["screenX"]       = ev.screenX;
        j["screenY"]       = ev.screenY;
    }

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
    ev.id          = j.value("id", std::string());
    ev.continentX  = j.value("continentX", 0.0f);
    ev.continentY  = j.value("continentY", 0.0f);
    ev.isVarying   = j.value("isVarying", false);
    ev.duration    = j.value("duration", 0);
    ev.iconTexture = j.value("iconTexture", std::string());
    ev.chatCode    = j.value("chatCode", std::string());
    ev.shown       = j.value("shown", true);
    ev.customName  = j.value("customName", std::string());

    ev.fixedToScreen = j.value("fixedToScreen", false);
    ev.screenX        = j.value("screenX", 0.5f);
    ev.screenY        = j.value("screenY", 0.5f);

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
// within the group (see SlotKey), assigned up front when the slot is created -
// same as SerializeEvent/DeserializeEvent above. customColor is presence-checked
// (j.contains), not defaulted, so "unset" round-trips exactly;
// chatCode/customName omitted when empty, shown when true (the default), same
// convention as WorldEvent above. isVarying/varyingTimes follow WorldEvent's own
// convention: isVarying always written, varyingTimes only written/read when
// isVarying is true.
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

static CyclicGroup::Slot DeserializeSlot(const json& j)
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

    slot.chatCode   = j.value("chatCode", std::string());
    slot.shown      = j.value("shown", true);
    slot.customName = j.value("customName", std::string());

    return slot;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeGroup / DeserializeGroup
//--------------------------------------------------------------------------------
// (De)serializes one CyclicGroup, including its nested slots array via
// SerializeSlot/DeserializeSlot - abandoned (unnamed, non-default) slots are
// dropped on serialize, same as SaveEventsData (IsAbandonedEntry above). id is
// the merge/identity key (see GroupKey), assigned up front when the group is
// created - same as SerializeEvent/DeserializeEvent above. idleColor is presence-
// checked like Slot::customColor above; shown/customName are omitted when
// true/empty (their defaults), same convention as WorldEvent above.
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

    if (grp.fixedToScreen)
    {
        j["fixedToScreen"] = true;
        j["screenX"]       = grp.screenX;
        j["screenY"]       = grp.screenY;
    }

    json slots = json::array();
    for (const auto& slot : grp.slots)
        if (!IsAbandonedEntry(slot.customName, GetDefaultCyclicSlot(grp.id, slot.id) != nullptr))
            slots.push_back(SerializeSlot(slot));
    j["slots"] = slots;

    return j;
}

static CyclicGroup DeserializeGroup(const json& j)
{
    CyclicGroup grp{};
    grp.id         = j.value("id", std::string());
    grp.continentX = j.value("continentX", 0.0f);
    grp.continentY = j.value("continentY", 0.0f);
    grp.period     = j.value("period", 7200);
    grp.colors     = ColorSet{ DeserializeColorArray(j.value("colors", json()), ImVec4(0.502f, 0.502f, 0.502f, 1.0f)) };   //. matches old #808080FF default

    if (j.contains("idleColor"))
        grp.idleColor = HexStringToColor(j.value("idleColor", std::string()), 0xFFFFFFFFu);

    grp.shown      = j.value("shown", true);
    grp.customName = j.value("customName", std::string());

    grp.fixedToScreen = j.value("fixedToScreen", false);
    grp.screenX        = j.value("screenX", 0.5f);
    grp.screenY        = j.value("screenY", 0.5f);

    if (j.contains("slots") && j["slots"].is_array())
        for (const auto& sj : j["slots"])
            grp.slots.push_back(DeserializeSlot(sj));

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
// A default missing from loaded is always added: a compiled-in default can only
// be hidden (shown = false) or overridden, never truly deleted, so a missing key
// here means the default is new since the file was last saved, not user-removed -
// see EVENTS_DATA_VERSION (events.h).
//--------------------------------------------------------------------------------
template<typename T, typename KeyFn>
static std::vector<T> MergeByKey(const std::vector<T>& defaults, const std::vector<T>& loaded, KeyFn getKey)
{
    std::unordered_map<std::string, size_t> loadedIndexByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByKey[getKey(loaded[i])] = i;   //. last one wins on a malformed duplicate key

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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GroupKey / SlotKey / EventKey
//--------------------------------------------------------------------------------
// Merge keys for MergeGroups/MergeByKey. Groups, events, and slots all key on id
// - for slots this means unique WITHIN the group, not globally (see
// CyclicGroup::Slot::id, events.h). name is display-only and can change (user
// rename, localization) without breaking the merge match.
//--------------------------------------------------------------------------------
static std::string GroupKey(const CyclicGroup& g) { return g.id; }
static std::string SlotKey(const CyclicGroup::Slot& s) { return s.id; }
static std::string EventKey(const WorldEvent& e) { return e.id; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MergeGroups
//--------------------------------------------------------------------------------
// MergeByKey for CyclicGroup, plus one extra pass: even when a group matches by
// key, its slots are still merged one level deeper via MergeByKey, so a new slot
// added to that group in a newer build still appears - the loaded group object
// doesn't replace it wholesale. Same unconditional-resurrect rule as MergeByKey,
// at both levels: a default group or slot missing from loaded is always added.
//--------------------------------------------------------------------------------
static std::vector<CyclicGroup> MergeGroups(const std::vector<CyclicGroup>& defaults, const std::vector<CyclicGroup>& loaded)
{
    std::unordered_map<std::string, size_t> loadedIndexByKey;
    for (size_t i = 0; i < loaded.size(); i++)
        loadedIndexByKey[GroupKey(loaded[i])] = i;   //. last one wins on a malformed duplicate key

    std::vector<bool> matched(loaded.size(), false);
    std::vector<CyclicGroup> result;
    result.reserve(defaults.size() + loaded.size());

    for (const auto& def : defaults)
    {
        auto it = loadedIndexByKey.find(GroupKey(def));
        if (it != loadedIndexByKey.end())
        {
            CyclicGroup merged = loaded[it->second];
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
// SlugifyName   (see: events_storage.h)
//--------------------------------------------------------------------------------
// Lowercases, drops apostrophes, and collapses every other non-alphanumeric run
// to a single underscore (leading/trailing underscores stripped). Same scheme
// used by hand for the compiled-in ids (events_basic.cpp/events_cyclic.cpp).
// Migration fallback only, for a loaded name that doesn't match any compiled-in
// default's DisplayNameEnglish - see LoadEventsData below and
// events_categories.cpp's own Category id-assignment pass.
//--------------------------------------------------------------------------------
std::string SlugifyName(const std::string& name)
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
// UniqueId   (see: events_storage.h)
//--------------------------------------------------------------------------------
// Returns `candidate`, or "candidate_2"/"_3"/... if it's already in `used`, and
// reserves whichever id it returns. Only matters for SlugifyName fallbacks - two
// differently-punctuated names can slugify to the same string.
//--------------------------------------------------------------------------------
std::string UniqueId(const std::string& candidate, std::unordered_set<std::string>& used)
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
// SaveEventsData serializes g_Events/g_CyclicGroups to events.json, dropping any
// abandoned unnamed entry along the way (IsAbandonedEntry above) so it isn't
// written as a permanent "(unnamed)" row - the in-memory vectors themselves are
// untouched, only what gets written. LoadEventsData merges disk contents into
// them (see MergeByKey/MergeGroups, id backfill block below), then restamps
// apiWorldBossId/doneGroup/apiMapChestId from the compiled-in defaults by id,
// since those cross-reference fields are never read from or written to the file.
// A missing file isn't an error - g_Events/g_CyclicGroups are simply left at
// their compiled-in defaults.
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
            if (!IsAbandonedEntry(ev.customName, GetDefaultEvent(ev.id) != nullptr))
                eventsArr.push_back(SerializeEvent(ev));
        j["events"] = eventsArr;

        json groupsArr = json::array();
        for (const auto& grp : g_CyclicGroups)
            if (!IsAbandonedEntry(grp.customName, GetDefaultCyclicGroup(grp.id) != nullptr))
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

        //_ Feeds only the version-gated one-time corrections below (ApplyCategoryOffsetOverrides/ApplyCategoryDurationOverrides/ApplySlotOverrides) - the merge itself no longer needs it.
        int64_t savedVersion = j.value("data_version", (int64_t)0);

        std::vector<WorldEvent> loadedEvents;
        if (j.contains("events") && j["events"].is_array())
            for (const auto& ej : j["events"])
                loadedEvents.push_back(DeserializeEvent(ej));

        std::vector<CyclicGroup> loadedGroups;
        if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
            for (const auto& gj : j["cyclicGroups"])
                loadedGroups.push_back(DeserializeGroup(gj));

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // (id backfill)
        //--------------------------------------------------------------------------------
        // The options panel's "+" button assigns an id up front (see options_events.cpp),
        // so a loaded id is only ever empty for a save written by a build predating that
        // fix - see SerializeEvent/SerializeSlot/SerializeGroup and SlugifyName/UniqueId
        // (events_storage.h). Everything else on disk already carries a real id.
        //--------------------------------------------------------------------------------
        std::unordered_set<std::string> usedEventIds;
        for (const auto& d : g_Events) usedEventIds.insert(d.id);
        for (auto& ev : loadedEvents)
        {
            if (ev.id.empty()) ev.id = UniqueId(SlugifyName(ev.customName), usedEventIds);
            else                usedEventIds.insert(ev.id);
        }

        std::unordered_set<std::string> usedGroupIds;
        for (const auto& d : g_CyclicGroups) usedGroupIds.insert(d.id);
        for (auto& grp : loadedGroups)
        {
            if (grp.id.empty()) grp.id = UniqueId(SlugifyName(grp.customName), usedGroupIds);
            else                usedGroupIds.insert(grp.id);

            //_ Slot ids only need to be unique within this group.
            std::unordered_set<std::string> usedSlotIds;
            for (const auto& slot : grp.slots)
                if (!slot.id.empty()) usedSlotIds.insert(slot.id);
            for (auto& slot : grp.slots)
                if (slot.id.empty()) slot.id = UniqueId(SlugifyName(slot.customName), usedSlotIds);
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

        g_Events = MergeByKey(g_Events, loadedEvents, EventKey);
        ApplyCategoryOffsetOverrides(g_Events, savedVersion);
        ApplyCategoryDurationOverrides(g_Events, savedVersion);

        g_CyclicGroups = MergeGroups(g_CyclicGroups, loadedGroups);
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
// RestoreMissingDefaults   (see: events_storage.h)
//--------------------------------------------------------------------------------
// MergeByKey/MergeGroups run here exactly as they do in LoadEventsData, just
// against the current live g_Events/g_CyclicGroups instead of a freshly-loaded
// JSON snapshot - "defaults" is still s_compiledDefaultEvents/
// s_compiledDefaultGroups, never the live lists themselves, so a default that's
// been renamed or otherwise edited is correctly seen as "present" (it matches by
// id) and is left as the player has it, not reverted.
//--------------------------------------------------------------------------------
int RestoreMissingDefaults()
{
    if (!s_compiledDefaultsCaptured) return 0;

    size_t beforeEvents = g_Events.size();
    size_t beforeGroups = g_CyclicGroups.size();
    size_t beforeSlots  = 0;
    for (const auto& grp : g_CyclicGroups) beforeSlots += grp.slots.size();

    g_Events       = MergeByKey(s_compiledDefaultEvents, g_Events, EventKey);
    g_CyclicGroups = MergeGroups(s_compiledDefaultGroups, g_CyclicGroups);

    size_t afterSlots = 0;
    for (const auto& grp : g_CyclicGroups) afterSlots += grp.slots.size();

    return (int)(g_Events.size() - beforeEvents)
         + (int)(g_CyclicGroups.size() - beforeGroups)
         + (int)(afterSlots - beforeSlots);
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