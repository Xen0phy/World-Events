// weekly_vault.cpp
// See weekly_vault.h for the overall design. This file is ONLY the static
// mapping table + the two lookup functions that walk it — all the live API
// state (what's actually in this week's rotation, what's already complete)
// comes from gw2_api.h's GetWeeklyObjectiveState, called from here.
//
// `title` must match the objective's title from
// /v2/account/wizardsvault/weekly verbatim (case-insensitive, but
// otherwise exact — see GetWeeklyObjectiveState). groupOrEventName/
// slotName must match an EXISTING WorldEvent::name (events_basic.cpp) or
// CyclicGroup::name + Slot::name (events_cyclic.cpp) exactly — these are
// the addon's own internal names, not necessarily the in-game event's
// displayed name. A mismatch anywhere in this table just means that
// target silently never lights up; nothing will warn you, so if a
// normally-rotating objective stops tracking, check here first.
//
// For a Cyclic target, point at the ONE (or few) slot(s) that actually
// complete the meta, not the whole group — most groups have build-up
// slots that don't finish anything on their own.

#include "weekly_vault.h"
#include "gw2_api.h"

std::vector<WeeklyObjectiveMapping> g_WeeklyObjectives =
{
    // Basic (World Bosses) — 1:1 with a single WorldEvent each. Titles
    // include ArenaNet's "or Complete Events in <map>" fallback clause.
    { "Defeat the Claw of Jormag World Boss or Complete Events in Frostgorge Sound",        { {"Claw of Jormag", ""} } },
    { "Defeat the Fire Elemental World Boss or Complete Events in Metrica Province",        { {"Fire Elemental", ""} } },
    { "Defeat the Great Jungle Wurm World Boss or Complete Events in Caledon Forest",       { {"Great Jungle Wurm", ""} } },
    { "Defeat the Inquest Golem Mark II World Boss or Complete Events in Mount Maelstrom",  { {"Golem Mark II", ""} } }, // addon's name omits "Inquest"
    { "Defeat the Megadestroyer World Boss or Complete Events in Mount Maelstrom",          { {"Megadestroyer", ""} } },
    { "Defeat the Shadow Behemoth World Boss or Complete Events in Queensdale",             { {"Shadow Behemoth", ""} } },
    { "Defeat the Shatterer World Boss or Complete Events in Blazeridge Steppes",           { {"The Shatterer", ""} } },
    { "Defeat the Svanir Shaman Chief World Boss or Complete Events in Wayfarer Foothills", { {"Svanir Shaman Chief", ""} } },
    { "Defeat Tequatl the Sunless or Complete Events in Sparkfly Fen",                      { {"Tequatl the Sunless", ""} } },

    // Cyclic (Meta events)
    { "Complete a Meta-Event or Events in Cantha or Events in Maguuma Jungle", {
        { "Seitung Province",   "Aetherblade Assault" },
        { "New Kaineng City",   "Kaineng Blackout" },
        { "The Echovald Wilds", "Gang War" },
        { "Dragon's End", "Battle for the Jade Sea" },
    }},
    { "Complete a Meta-Event or Events in Castora or Events in Orr", {
        { "Shipwreck Strand", "Hammerheart Rumble" },
        { "Starlit Weald",    "Secrets of the Weald" },
    }},
    { "Complete a Meta-Event or Events in Heart of Maguuma or Events in Ascalon", {
        { "Auric Basin",     "Octovine" },
        { "Tangled Depths",  "Chak Gerent" },
        { "Dragon's Stand",  "Mordremoth Start" },
    }},
    { "Complete a Meta-Event or Events in Horn of Maguuma or Events in Shiverpeak Mountains", {
        { "Skywatch Archipelago", "Unlocking the Wizard's Tower" }, // not the separate "Wizard's Tower" CyclicGroup
        { "Amnytas",         "Defense of Amnytas" },
    }},
    { "Complete a Meta-Event or Events in Janthir or Events in Orr", {
        { "Janthir Syntri", "Of Mists and Monsters" },
        { "Bava Nisos",     "A Titanic Voyage" },
    }},
    { "Complete a Meta-Event or Events in the Crystal Desert or Events in Kryta", {
        { "Elon Riverlands", "The Path to Ascension" },
        { "The Desolation",  "Maws of Torment" },
        { "Domain of Vabbi", "Forged with Fire" },
    }},
};

bool IsBasicEventWeeklyTarget(const std::string& eventName, bool& outComplete)
{
    for (const auto& mapping : g_WeeklyObjectives)
    {
        for (const auto& target : mapping.targets)
        {
            if (!target.slotName.empty()) continue; // a Cyclic target, not this function's concern
            if (target.groupOrEventName != eventName) continue;

            WeeklyObjectiveState state = GetWeeklyObjectiveState(mapping.title);
            if (state == WeeklyObjectiveState::NotThisWeek) continue; // this objective isn't part of the live rotation right now — not a match this week

            outComplete = (state == WeeklyObjectiveState::Complete);
            return true;
        }
    }
    return false;
}

bool IsCyclicSlotWeeklyTarget(const std::string& groupName, const std::string& slotName, bool& outComplete)
{
    for (const auto& mapping : g_WeeklyObjectives)
    {
        for (const auto& target : mapping.targets)
        {
            if (target.slotName.empty()) continue; // a Basic Event target, not this function's concern
            if (target.groupOrEventName != groupName || target.slotName != slotName) continue;

            WeeklyObjectiveState state = GetWeeklyObjectiveState(mapping.title);
            if (state == WeeklyObjectiveState::NotThisWeek) continue;

            outComplete = (state == WeeklyObjectiveState::Complete);
            return true;
        }
    }
    return false;
}
