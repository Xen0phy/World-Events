// weekly_vault.cpp
// See weekly_vault.h for the overall design. This file is ONLY the static
// Cyclic mapping table + the two lookup functions that check against it —
// all the live API state (what's actually in this week's rotation, what's
// already complete) comes from gw2_api.h's GetLiveWeeklyObjectives, called
// from here. Core Boss matching needs no table at all (see weekly_vault.h).
//
// `titleKeywords` entries must be exact substrings (case-insensitive) of
// the objective's real title from /v2/account/wizardsvault/weekly — these
// are usually just the objective's own "Events in <region>" clauses (see
// weekly_vault.h), not the addon's own internal names. A keyword that stops
// matching just means that mapping silently never lights up at runtime;
// nothing will warn you, so if a normally-rotating objective stops
// tracking, check here first. (check_weekly_vault.py, run at build time,
// catches the OTHER half of this table going stale — a `targets` entry
// that no longer resolves against events_cyclic.cpp — but it can't verify
// titleKeywords against ArenaNet's live wording; nothing offline can.)
//
// groupName/slotName in `targets` must match an EXISTING CyclicGroup::name
// + Slot::name (events_cyclic.cpp) exactly — these are the addon's own
// internal names, not necessarily the in-game event's displayed name.
//
// For a Cyclic target, point at the ONE (or few) slot(s) that actually
// complete the meta, not the whole group — most groups have build-up
// slots that don't finish anything on their own.

#include "weekly_vault.h"
#include "gw2_api.h"
#include "events.h"
#include <algorithm>
#include <cctype>

std::vector<CyclicWeeklyMapping> g_CyclicWeeklyObjectives =
{
    { {"Cantha", "Maguuma Jungle"}, {
        { "Seitung Province",   "Aetherblade Assault" },
        { "New Kaineng City",   "Kaineng Blackout" },
        { "The Echovald Wilds", "Gang War" },
        { "Dragon's End", "Battle for the Jade Sea" },
    }},
    { {"Castora", "Orr"}, {
        { "Shipwreck Strand", "Hammerhart Rumble" },
        { "Starlit Weald",    "Secrets of the Weald" },
    }},
    { {"Heart of Maguuma", "Ascalon"}, {
        { "Auric Basin",     "Octovine" },
        { "Tangled Depths",  "Chak Gerent" },
        { "Dragon's Stand",  "Mordremoth Start" },
    }},
    { {"Horn of Maguuma", "Shiverpeak Mountains"}, {
        { "Skywatch Archipelago", "Unlocking the Wizard's Tower" }, // not the separate "Wizard's Tower" CyclicGroup
        { "Amnytas",         "Defense of Amnytas" },
    }},
    { {"Janthir", "Orr"}, {
        { "Janthir Syntri", "Of Mists and Monsters" },
        { "Bava Nisos",     "A Titanic Voyage" },
    }},
    { {"Crystal Desert", "Kryta"}, {
        { "Elon Riverlands", "The Path to Ascension" },
        { "The Desolation",  "Maws of Torment" },
        { "Domain of Vabbi", "Forged with Fire" },
    }},
};

// ASCII-only lowercase, for case-insensitive substring matching against
// GetLiveWeeklyObjectives' titles (already lowercased on that side too —
// see gw2_api.cpp's own AsciiLower). Every boss name / titleKeyword used
// here is plain ASCII, so no locale/UTF-8 handling is needed. Not shared
// with gw2_api.cpp's copy since that one is file-static there — small
// enough to not be worth a shared header just for this.
static std::string AsciiLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}

bool IsBasicEventWeeklyTarget(const std::string& eventName, bool& outComplete)
{
    // Only Core Bosses (non-empty apiWorldBossId) are ever part of the
    // Vault's rotation — confirm eventName actually IS one before bothering
    // to search the live list at all.
    auto evIt = std::find_if(g_Events.begin(), g_Events.end(),
        [&](const WorldEvent& e) { return e.name == eventName; });
    if (evIt == g_Events.end() || evIt->apiWorldBossId.empty()) return false;

    std::string needle = AsciiLower(eventName);
    for (const auto& live : GetLiveWeeklyObjectives())
    {
        if (live.titleLower.find(needle) == std::string::npos) continue; // not this one — keep looking, in case of an unrelated title that happens to share other words

        outComplete = live.complete;
        return true;
    }
    return false;
}

bool IsCyclicSlotWeeklyTarget(const std::string& groupName, const std::string& slotName, bool& outComplete)
{
    for (const auto& mapping : g_CyclicWeeklyObjectives)
    {
        bool isTarget = std::any_of(mapping.targets.begin(), mapping.targets.end(),
            [&](const CyclicWeeklyTarget& t) { return t.groupName == groupName && t.slotName == slotName; });
        if (!isTarget) continue; // this mapping doesn't concern the slot being asked about

        for (const auto& live : GetLiveWeeklyObjectives())
        {
            bool allKeywordsMatch = std::all_of(mapping.titleKeywords.begin(), mapping.titleKeywords.end(),
                [&](const std::string& kw) { return live.titleLower.find(AsciiLower(kw)) != std::string::npos; });
            if (!allKeywordsMatch) continue; // not this live objective — a different one might still match

            outComplete = live.complete;
            return true;
        }
    }
    return false;
}
