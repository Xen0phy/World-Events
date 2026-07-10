// weekly_vault.cpp
// See weekly_vault.h for the overall design. This file is ONLY the static
// mapping table + the two lookup functions that walk it — all the live API
// state (what's actually in this week's rotation, what's already complete)
// comes from gw2_api.h's GetWeeklyObjectiveState, called from here.
//
// ---------------------------------------------------------------------------
// EDITING THIS TABLE
// ---------------------------------------------------------------------------
// Each WeeklyObjectiveMapping's `title` should match, verbatim (case
// doesn't matter — GetWeeklyObjectiveState lowercases both sides), the
// objective's own title as shown in-game / returned by
// /v2/account/wizardsvault/weekly. If ArenaNet rewords an objective's
// title in a future update, matching here silently stops working — the
// symptom is that objective's targets simply never light up as "weekly"
// even though it's live in the Vault that week. There is no way to detect
// that automatically (the API gives no stable id to fall back to, only
// the title itself), so if a normally-rotating objective seems to have
// stopped tracking, this file is the first thing to check.
//
// Each WeeklyTarget's groupOrEventName/slotName must match an EXISTING
// WorldEvent::name (events_basic.cpp) or CyclicGroup::name +
// CyclicGroup::Slot::name (events_cyclic.cpp) exactly — these are NOT
// necessarily the same as the in-game event's own displayed name (see the
// "Golem Mark II" example below). A typo here just means that target
// silently never matches anything; nothing will warn you.
//
// For a Cyclic target, deliberately point at the ONE (or few) slot(s)
// that actually complete the meta, not the whole group — most groups have
// build-up slots that don't finish anything on their own. "Battle in
// Tarir" (Auric Basin) is the example that prompted this: only its
// "Octovine" slot finishes the meta; "Challenges"/"Pylons" are earlier
// steps in the same 2h cycle and are deliberately NOT listed as targets.
//
// Known gaps/ambiguities in the mappings below, from the objective list
// this file was written against — check these against the current
// in-game Vault board before relying on them:
//   - "Destroy the Ravenous Wanderer" (Cantha/Maguuma objective) —
//     CONFIRMED not mappable: it has no fixed schedule (doesn't run on a
//     timer this addon's period/offset model can represent), not a
//     missing-lookup gap. Left unmapped permanently, not a TODO.
//   - "The Spider's Lair" (Horn of Maguuma/Shiverpeak objective) —
//     CONFIRMED not mappable, same reason as above (no fixed schedule).
//     Left unmapped permanently, not a TODO.
//   - "Kaineng Blackout" — CONFIRMED: belongs to New Kaineng City only.
//     Does NOT also credit from The Echovald Wilds' own same-named slot
//     (removed below; that slot exists in events_cyclic.cpp but is a
//     different, unrelated occurrence, not the same in-game event).
//   - "Gang War of Echovald" is spelled "Gang War" in events_cyclic.cpp —
//     CONFIRMED same event despite the shortened addon-internal name, no
//     mapping change needed.
//   - "Hammerheart Rumble!" is spelled "Hammerhart Rumble" in
//     events_cyclic.cpp — CONFIRMED a known pre-existing typo (not a
//     different event); mapped as-is since other code already keys off
//     this exact addon-internal name, a rename is a separate/wider change.
//   - "Wizard's Tower" objective — CONFIRMED: means unlocking the
//     Wizard's Tower itself, i.e. the "Skywatch Archipelago" group's own
//     "Unlocking the Wizard's Tower" slot. NOT the separate "Wizard's
//     Tower" CyclicGroup ("Target Practice"/"Fly by Night") — neither of
//     those slots credits this objective.
//   - "Dragon's Stand" objective — CONFIRMED: "Mordremoth Start" (the
//     opening beacon-lighting phase) is the slot that credits the
//     objective, not "Mordremoth Progress".
// ---------------------------------------------------------------------------

#include "weekly_vault.h"
#include "gw2_api.h"

std::vector<WeeklyObjectiveMapping> g_WeeklyObjectives =
{
    // -----------------------------------------------------------------
    // Basic (World Bosses) — 1:1 with a single WorldEvent each.
    //
    // Every title below includes the "or Complete Events in <map>"
    // fallback clause ArenaNet appends to the live API's own title —
    // GetWeeklyObjectiveState is exact-match only (see gw2_api.h), so a
    // title missing this clause silently never matches and that boss's
    // isWeekly/red-dot marker never lights up, even in a week it's
    // genuinely live in the Vault rotation. If a title here ever stops
    // matching, re-check it against a fresh /v2/account/wizardsvault/weekly
    // response rather than assuming the mapping itself is wrong.
    // -----------------------------------------------------------------
    { "Defeat the Claw of Jormag World Boss or Complete Events in Frostgorge Sound",        { {"Claw of Jormag", ""} } },
    { "Defeat the Fire Elemental World Boss or Complete Events in Metrica Province",        { {"Fire Elemental", ""} } },
    { "Defeat the Great Jungle Wurm World Boss or Complete Events in Caledon Forest",       { {"Great Jungle Wurm", ""} } },
    // Addon's own name for this one is "Golem Mark II", not "Inquest
    // Golem Mark II" — see events_basic.cpp.
    { "Defeat the Inquest Golem Mark II World Boss or Complete Events in Mount Maelstrom",  { {"Golem Mark II", ""} } },
    { "Defeat the Megadestroyer World Boss or Complete Events in Mount Maelstrom",          { {"Megadestroyer", ""} } },
    { "Defeat the Shadow Behemoth World Boss or Complete Events in Queensdale",             { {"Shadow Behemoth", ""} } },
    { "Defeat the Shatterer World Boss or Complete Events in Blazeridge Steppes",           { {"The Shatterer", ""} } },
    { "Defeat the Svanir Shaman Chief World Boss or Complete Events in Wayfarer Foothills", { {"Svanir Shaman Chief", ""} } },
    // NOTE: missing "World Boss" compared to every other entry in this
    // block (also inconsistent with the "Defeat the Tequatl the Sunless
    // World Boss or Complete Events in Sparkfly Fen" wording seen in
    // ArenaNet's Feb 3 2026 patch notes) — kept exactly as supplied since
    // this function is exact-match and the live in-game string is the
    // only source of truth; re-verify against a live API response if this
    // one doesn't light up.
    { "Defeat Tequatl the Sunless or Complete Events in Sparkfly Fen",                      { {"Tequatl the Sunless", ""} } },

    // -----------------------------------------------------------------
    // Cyclic (Meta events) — see the big file-level comment above before
    // editing any of this, especially the gaps/ambiguities list.
    // -----------------------------------------------------------------
    { "Complete a Meta-Event or Events in Cantha or Events in Maguuma Jungle", {
        { "Seitung Province",   "Aetherblade Assault" },
        { "New Kaineng City",   "Kaineng Blackout" },   // CONFIRMED — "Kaineng Blackout" belongs to New Kaineng City only, NOT The Echovald Wilds' own same-named slot (removed below)
        { "The Echovald Wilds", "Gang War" },           // "Gang War of Echovald" in-game/Vault wording — confirmed same event despite the shortened addon-internal name, no change needed
        // "Destroy the Ravenous Wanderer" — confirmed NOT MAPPABLE: no
        // fixed schedule (doesn't run on a timer this addon's
        // period/offset model can represent), not a missing-lookup gap.
        { "Dragon's End", "Battle for the Jade Sea" },
    }},
    { "Complete a Meta-Event or Events in Castora or Events in Orr", {
        { "Shipwreck Strand", "Hammerhart Rumble" },    // CONFIRMED correct target — "Hammerhart Rumble" is a known pre-existing typo in events_cyclic.cpp for "Hammerheart Rumble!" (the in-game/Vault wording); not renamed here since other code already keys off this exact addon-internal name (subscriptions, chat codes) — a rename is a separate, wider change, not this file's concern.
        { "Starlit Weald",    "Secrets of the Weald" },
    }},
    { "Complete a Meta-Event or Events in Heart of Maguuma or Events in Ascalon", {
        // Only the climactic slot of each map, not the whole ring — see
        // the "Battle in Tarir" example in the file-level comment above.
        { "Auric Basin",     "Octovine" },      // "Battle in Tarir"
        { "Tangled Depths",  "Chak Gerent" },    // "King of the Jungle"
        { "Dragon's Stand",  "Mordremoth Start" }, // CONFIRMED — the opening beacon-lighting phase credits this objective, not "Mordremoth Progress"
    }},
    { "Complete a Meta-Event or Events in Horn of Maguuma or Events in Shiverpeak Mountains", {
        // CONFIRMED: the objective means unlocking the Wizard's Tower
        // itself (the "Skywatch Archipelago" group's own slot), NOT
        // anything in the separate "Wizard's Tower" CyclicGroup
        // ("Target Practice"/"Fly by Night") — neither of those slots
        // credits this objective.
        { "Skywatch Archipelago", "Unlocking the Wizard's Tower" },
        { "Amnytas",         "Defense of Amnytas" },
        // "The Spider's Lair" — confirmed NOT MAPPABLE: no fixed
        // schedule, not a missing-lookup gap.
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
