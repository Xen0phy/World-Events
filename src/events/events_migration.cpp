//################################################################################
// events_migration.cpp
//--------------------------------------------------------------------------------
// Pre-1.8.0.0 events.json is the old name-keyed format (WorldEvent/CyclicGroup,
// no id/customName - see events.h/events_storage.h). name/chatCode/icon/duration/
// offset/period/colors were all independently user-editable in the old UI, so
// none of them can identify the same row reliably; continentX/continentY never
// had a UI control at all, so they're the only field a rename or chat-code edit
// couldn't have touched. Matching is therefore by coordinate alone - (continentX,
// continentY) for a Basic Event or a Cyclic Group, plus the matched group's slot
// offsets for its Slots. Everything else (chatCode, icon, duration,
// offset/period, colors, categories, Live Events, and any rename) stays at the
// compiled default - carrying it forward would mean trusting the very fields that
// make matching unreliable in the first place.
//
// A group whose own compiled-in slots don't have unique offsets to begin with -
// Dry Top, whose isVarying slots mostly share a meaningless offset of 0 (see
// events_cyclic.cpp) - can't be matched at the slot level at all, so its slots
// stay at their fresh compiled defaults unconditionally; only the group's own
// "shown" still migrates normally.
//
// An old row with no coordinate match (a genuine custom "+"-created entry) is
// left out of the migration entirely, not resurrected under a new id.
//--------------------------------------------------------------------------------

#include "events_migration.h"

#include "events.h"
#include "settings.h"
#include "subscriptions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>
#include <utility>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // CoordKey / MakeCoordKey
    //--------------------------------------------------------------------------------
    // Rounds continentX/Y to whole units for use as an unordered_map key. Every
    // compiled-in row's coordinates are whole numbers already (see events_basic.cpp/
    // events_cyclic.cpp) - the rounding only guards against float drift from a JSON
    // round-trip, not real fractional coordinates.
    //--------------------------------------------------------------------------------
    struct CoordKey
    {
        long long x, y;
        bool operator==(const CoordKey& o) const { return x == o.x && y == o.y; }
    };
    struct CoordKeyHash
    {
        size_t operator()(const CoordKey& k) const
        {
            return std::hash<long long>()(k.x) ^ (std::hash<long long>()(k.y) << 1);
        }
    };
    CoordKey MakeCoordKey(float x, float y)
    {
        return CoordKey{ (long long)std::lround(x), (long long)std::lround(y) };
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // LegacyCyclicKey / LegacyNotifyLevel
    //--------------------------------------------------------------------------------
    // Reimplements GetBasicEventNotifyLevel/GetCyclicSlotNotifyLevel's ladder
    // (subscribed -> +toast -> +sound, see subscriptions.cpp) directly over the raw
    // legacy JSON arrays: the old (name) / (groupName, slotOffset) key shapes don't
    // fit the live g_Subscribed... globals' new (id) / (groupId, slotId) shape, so
    // this reads the legacy arrays on their own terms instead of forcing them through
    // the current helpers.
    //--------------------------------------------------------------------------------
    struct LegacyCyclicKey
    {
        std::string groupName;
        int         slotOffset = 0;
        bool operator==(const LegacyCyclicKey& o) const
        {
            return groupName == o.groupName && slotOffset == o.slotOffset;
        }
    };
    struct LegacyCyclicKeyHash
    {
        size_t operator()(const LegacyCyclicKey& k) const
        {
            return std::hash<std::string>()(k.groupName) ^ (std::hash<int>()(k.slotOffset) << 1);
        }
    };

    template<typename Key>
    int LegacyNotifyLevel(const Key& key, const std::vector<Key>& subscribed,
                           const std::vector<Key>& toast, const std::vector<Key>& sound)
    {
        auto has = [&](const std::vector<Key>& v) { return std::find(v.begin(), v.end(), key) != v.end(); };
        if (!has(subscribed)) return 0;
        if (!has(toast))      return 1;
        return has(sound) ? 3 : 2;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MigrateLegacyEventsFile   (see: events_migration.h)
//--------------------------------------------------------------------------------
void MigrateLegacyEventsFile(const std::string& addonDir)
{
    if (LastKnownVersion >= 1080000) return;

    std::string filepath = addonDir + "\\events.json";

    json j;
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return; //. no legacy file, nothing to migrate

        try { j = json::parse(file); }
        catch (...) { return; } //. unreadable - nothing salvageable, leave compiled defaults untouched
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Basic Events   (see: events_migration.h)
    //--------------------------------------------------------------------------------
    std::unordered_map<CoordKey, size_t, CoordKeyHash> eventIndexByCoord;
    for (size_t i = 0; i < g_Events.size(); i++)
        eventIndexByCoord[MakeCoordKey(g_Events[i].continentX, g_Events[i].continentY)] = i;

    //_ Only entries that matched a default above end up here - see events_migration.h.
    std::unordered_map<std::string, std::string> legacyEventNameToId;

    if (j.contains("events") && j["events"].is_array())
    {
        for (const auto& ej : j["events"])
        {
            auto it = eventIndexByCoord.find(MakeCoordKey(ej.value("continentX", 0.0f), ej.value("continentY", 0.0f)));
            if (it == eventIndexByCoord.end()) continue; //. no compiled-in match - custom entry, drop

            WorldEvent& def = g_Events[it->second];
            bool oldShown = ej.value("shown", true);
            if (oldShown != def.shown)
                def.shown = oldShown;

            legacyEventNameToId[ej.value("name", std::string())] = def.id;
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Cyclic Groups / Slots   (see: events_migration.h)
    //--------------------------------------------------------------------------------
    std::unordered_map<CoordKey, size_t, CoordKeyHash> groupIndexByCoord;
    for (size_t i = 0; i < g_CyclicGroups.size(); i++)
        groupIndexByCoord[MakeCoordKey(g_CyclicGroups[i].continentX, g_CyclicGroups[i].continentY)] = i;

    //_ Only slots in an unambiguous, matched group end up here - see events_migration.h.
    std::unordered_map<LegacyCyclicKey, std::pair<std::string, std::string>, LegacyCyclicKeyHash> legacySlotKeyToNew;

    if (j.contains("cyclicGroups") && j["cyclicGroups"].is_array())
    {
        for (const auto& gj : j["cyclicGroups"])
        {
            auto it = groupIndexByCoord.find(MakeCoordKey(gj.value("continentX", 0.0f), gj.value("continentY", 0.0f)));
            if (it == groupIndexByCoord.end()) continue; //. no compiled-in match - custom entry, drop

            CyclicGroup& def = g_CyclicGroups[it->second];
            std::string legacyGroupName = gj.value("name", std::string());

            bool oldGroupShown = gj.value("shown", true);
            if (oldGroupShown != def.shown)
                def.shown = oldGroupShown;

            //_ A group whose own default slots don't have unique offsets (Dry Top) can't be slot-matched at all - leave every slot at its fresh compiled default.
            std::unordered_map<int, size_t> slotIndexByOffset;
            bool ambiguous = false;
            for (size_t s = 0; s < def.slots.size() && !ambiguous; s++)
                if (!slotIndexByOffset.emplace(def.slots[s].offset, s).second)
                    ambiguous = true;

            if (ambiguous || !gj.contains("slots") || !gj["slots"].is_array())
                continue;

            for (const auto& sj : gj["slots"])
            {
                int offset = sj.value("offset", 0);
                auto sit = slotIndexByOffset.find(offset);
                if (sit == slotIndexByOffset.end()) continue; //. no matching default slot, drop

                CyclicGroup::Slot& slotDef = def.slots[sit->second];
                bool oldSlotShown = sj.value("shown", true);
                if (oldSlotShown != slotDef.shown)
                    slotDef.shown = oldSlotShown;

                legacySlotKeyToNew[LegacyCyclicKey{ legacyGroupName, offset }] = { def.id, slotDef.id };
            }
        }
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Subscriptions / notify levels
    //--------------------------------------------------------------------------------
    // Only for entries matched above, and only when the resulting 0..3 level isn't
    // already 0 - the new build's default for everything, since g_Subscribed...
    // starts empty regardless. Categories, Live Events, and every other setting are
    // intentionally left out of this migration - see events_migration.h.
    //--------------------------------------------------------------------------------
    auto legacyBasicSub   = j.value("subscribedBasicEvents",   std::vector<std::string>{});
    auto legacyBasicToast = j.value("toastEnabledBasicEvents", std::vector<std::string>{});
    auto legacyBasicSound = j.value("soundEnabledBasicEvents", std::vector<std::string>{});

    for (const auto& [legacyName, newId] : legacyEventNameToId)
    {
        int level = LegacyNotifyLevel(legacyName, legacyBasicSub, legacyBasicToast, legacyBasicSound);
        if (level != 0)
            SetBasicEventNotifyLevel(newId, level);
    }

    auto readLegacyCyclicKeys = [&](const char* field)
    {
        std::vector<LegacyCyclicKey> out;
        if (j.contains(field) && j[field].is_array())
            for (const auto& kj : j[field])
                out.push_back(LegacyCyclicKey{ kj.value("groupName", std::string()), kj.value("slotOffset", 0) });
        return out;
    };
    auto legacyCyclicSub   = readLegacyCyclicKeys("subscribedCyclicSlots");
    auto legacyCyclicToast = readLegacyCyclicKeys("toastEnabledCyclicSlots");
    auto legacyCyclicSound = readLegacyCyclicKeys("soundEnabledCyclicSlots");

    for (const auto& [legacyKey, newIds] : legacySlotKeyToNew)
    {
        int level = LegacyNotifyLevel(legacyKey, legacyCyclicSub, legacyCyclicToast, legacyCyclicSound);
        if (level != 0)
            SetCyclicSlotNotifyLevel(CyclicSubscriptionKey{ newIds.first, newIds.second }, level);
    }

    //_ Everything worth keeping is now folded into g_Events/g_CyclicGroups/g_Subscribed... in memory; the legacy file's shape can't be merged the normal way (see events_migration.h), so it's removed here instead of being left for LoadEventsData to stumble over.
    std::error_code ec;
    fs::remove(filepath, ec);
}