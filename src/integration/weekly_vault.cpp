//################################################################################
// weekly_vault.cpp   (see: weekly_vault.h)
//--------------------------------------------------------------------------------
// g_CyclicWeeklyObjectives   the Cyclic objective table (see weekly_vault.h)
// AsciiLower                 lowercases for case-insensitive title matching
// IsBasicEventWeeklyTarget / IsCyclicSlotWeeklyTarget   see weekly_vault.h
//--------------------------------------------------------------------------------
// Core Boss objectives embed the boss's own display name verbatim, so any
// WorldEvent with a non-empty apiWorldBossId (events.h) is matched by name
// automatically - no table needed, and a newly-rotated-in boss needs zero edits.
// Cyclic objectives don't embed anything recognizable about the actual meta/map,
// so g_CyclicWeeklyObjectives below is a hand-maintained table instead; live
// rotation/completion state for both comes from gw2_api.h's
// GetLiveWeeklyObjectives.
//
// titleKeywords entries must be exact substrings (case-insensitive) of the
// objective's real title, usually just its "Events in <region>" clauses. A
// keyword that stops matching means the mapping silently stops lighting up -
// nothing will warn you. check_weekly_vault.py (build time) catches a targets
// entry that no longer resolves against events_cyclic.cpp, but can't verify
// titleKeywords against ArenaNet's live wording.
//
// groupId/slotId in targets must match an existing CyclicGroup::id + Slot::id
// (events_cyclic.cpp) exactly. Point at the slot(s) that actually complete the
// meta, not the whole group.
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_storage.h"
#include "gw2_api.h"
#include "weekly_vault.h"

#include <algorithm>
#include <cctype>

std::vector<CyclicWeeklyMapping> g_CyclicWeeklyObjectives =
{
    { {"Cantha", "Maguuma Jungle"}, {
        { "seitung_province",   "aetherblade_assault" },
        { "new_kaineng_city",   "kaineng_blackout" },
        { "the_echovald_wilds", "gang_war" },
        { "dragons_end", "battle_for_the_jade_sea" },
    }},
    { {"Castora", "Orr"}, {
        { "shipwreck_strand", "hammerhart_rumble" },
        { "starlit_weald",    "secrets_of_the_weald" },
    }},
    { {"Heart of Maguuma", "Ascalon"}, {
        { "auric_basin",     "octovine" },
        { "tangled_depths",  "chak_gerent" },
        { "dragons_stand",   "mordremoth_start" },
    }},
    { {"Horn of Maguuma", "Shiverpeak Mountains"}, {
        { "skywatch_archipelago", "unlocking_the_wizards_tower" }, //. different CyclicGroup, same slot title
        { "amnytas",              "defense_of_amnytas" },
    }},
    { {"Janthir", "Orr"}, {
        { "janthir_syntri", "of_mists_and_monsters" },
        { "bava_nisos",     "a_titanic_voyage" },
    }},
    { {"Crystal Desert", "Kryta"}, {
        { "elon_riverlands", "the_path_to_ascension" },
        { "the_desolation",  "maws_of_torment" },
        { "domain_of_vabbi", "forged_with_fire" },
    }},
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AsciiLower
//--------------------------------------------------------------------------------
// ASCII-only lowercase, for case-insensitive substring matching against
// GetLiveWeeklyObjectives' titles (already lowercased there too - see
// gw2_api.cpp's own AsciiLower). Every boss name/titleKeyword used here is plain
// ASCII, so no locale/UTF-8 handling is needed. Not shared with gw2_api.cpp's
// copy since that one is file-static there - too small to be worth a shared
// header just for this.
//--------------------------------------------------------------------------------
static std::string AsciiLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventWeeklyTarget   (pairs with: IsCyclicSlotWeeklyTarget)
//--------------------------------------------------------------------------------
// See weekly_vault.h for the full contract. Searches GetLiveWeeklyObjectives()
// (gw2_api.h) for a live title containing the event's COMPILED-IN DEFAULT name -
// GetDefaultEvent (events_storage.h), never the resolved event's own (possibly
// renamed/localized) name, since ArenaNet's API text never changes with it.
//--------------------------------------------------------------------------------
bool IsBasicEventWeeklyTarget(const std::string& eventId, bool& outComplete)
{
    //_ Only Core Bosses (non-empty apiWorldBossId) are ever in the Vault rotation.
    auto evIt = std::find_if(g_Events.begin(), g_Events.end(),
        [&](const WorldEvent& e) { return e.id == eventId; });
    if (evIt == g_Events.end() || evIt->apiWorldBossId.empty()) return false;

    //_ Every Core Boss is compiled-in; a missing default means eventId isn't actually one, so bail.
    const WorldEvent* defaultEv = GetDefaultEvent(eventId);
    if (!defaultEv) return false;

    std::string needle = AsciiLower(defaultEv->name);
    for (const auto& live : GetLiveWeeklyObjectives())
    {
        if (live.titleLower.find(needle) == std::string::npos) continue; //. not this one - keep looking

        outComplete = live.complete;
        return true;
    }
    return false;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotWeeklyTarget   (pairs with: IsBasicEventWeeklyTarget)
//--------------------------------------------------------------------------------
// See weekly_vault.h for the full contract. Searches g_CyclicWeeklyObjectives for
// a mapping listing this slot, then GetLiveWeeklyObjectives() for a live title
// matching ALL of that mapping's titleKeywords.
//--------------------------------------------------------------------------------
bool IsCyclicSlotWeeklyTarget(const std::string& groupId, const std::string& slotId, bool& outComplete)
{
    for (const auto& mapping : g_CyclicWeeklyObjectives)
    {
        bool isTarget = std::any_of(mapping.targets.begin(), mapping.targets.end(),
            [&](const CyclicWeeklyTarget& t) { return t.groupId == groupId && t.slotId == slotId; });
        if (!isTarget) continue; //. not this mapping's slot

        for (const auto& live : GetLiveWeeklyObjectives())
        {
            bool allKeywordsMatch = std::all_of(mapping.titleKeywords.begin(), mapping.titleKeywords.end(),
                [&](const std::string& kw) { return live.titleLower.find(AsciiLower(kw)) != std::string::npos; });
            if (!allKeywordsMatch) continue; //. not this objective - keep looking

            outComplete = live.complete;
            return true;
        }
    }
    return false;
}