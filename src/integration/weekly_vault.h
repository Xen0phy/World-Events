#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// weekly_vault.h
// ---------------------------------------------------------------------------
// Wizard's Vault WEEKLY objectives, cross-referenced against this addon's
// Basic Events / Cyclic Group slots.
//
// Completely separate system from, and independent of, the DAILY
// worldbosses/mapchests tracking in gw2_api.h / WorldEvent::apiWorldBossId /
// CyclicGroup::apiMapChestId (events.h). The two can overlap on the same
// physical boss/meta (e.g. killing Tequatl satisfies both the daily
// worldbosses list AND, in a week the Vault rotation includes it, this
// week's weekly objective) but are tracked on entirely separate reset
// schedules and separate live API calls (see gw2_api.h's
// GetLiveWeeklyObjectives, GET /v2/account/wizardsvault/weekly).
//
// Two different matching strategies below, since the two kinds of
// objective have very different title shapes:
//
// - Core Boss objectives ("Defeat the X World Boss or Complete Events in
//   Y") always embed the boss's own display name verbatim. So there's NO
//   separate table for these — any WorldEvent with a non-empty
//   apiWorldBossId (events.h) is automatically checked by name against
//   whatever's actually live this week (IsBasicEventWeeklyTarget below). A
//   boss ArenaNet newly rotates into the Vault is picked up with zero
//   edits needed anywhere in this addon.
// - Cyclic (meta-event) objectives ("...Events in Cantha or Events in
//   Maguuma Jungle") don't embed anything recognizable about the actual
//   meta/map, so there's no way to derive these automatically:
//   g_CyclicWeeklyObjectives below (weekly_vault.cpp) is still a hand-
//   maintained table, edited each time ArenaNet rotates the Cyclic list or
//   a mapping turns out wrong. Each entry only needs 1-2 short, distinctive
//   words pulled from the objective's title (see CyclicWeeklyMapping below)
//   rather than the whole sentence.
// ---------------------------------------------------------------------------

// One Cyclic Group slot that can satisfy a Cyclic weekly objective.
struct CyclicWeeklyTarget
{
    std::string groupName; // CyclicGroup::name
    std::string slotName;  // CyclicGroup::Slot::name within that group
};

// One Cyclic Wizard's Vault objective and every target that can satisfy it.
//
// `titleKeywords` are matched case-insensitively as independent substrings
// against a live objective's title — ALL of them have to appear somewhere
// in the SAME title for this mapping to count as "this week's". Two short,
// distinctive region names (the objective's own "Events in <region>"
// clauses) are normally enough to identify one Cyclic objective uniquely,
// without needing the full literal sentence, and without breaking if
// ArenaNet tweaks wording elsewhere in the title. Safe even when two
// different mappings happen to share one region name (e.g. two objectives
// both mentioning "Orr") — each live title is checked against one mapping's
// keywords at a time, so a title only matches a mapping whose keywords are
// ALL present in that specific title's own text. See weekly_vault.cpp.
//
// `targets` is an OR list, not an AND list — several real objectives span
// multiple, unrelated maps/chains at once, and completing ANY ONE of the
// listed targets is what actually completes the objective in-game. The
// live API only reports the objective's own aggregate progress, not which
// specific target was responsible — so once this objective is Complete,
// EVERY target listed here stops being treated as an active weekly target,
// regardless of which one the player actually did.
struct CyclicWeeklyMapping
{
    std::vector<std::string>       titleKeywords;
    std::vector<CyclicWeeklyTarget> targets;
};

// THE editable table for Cyclic objectives — see weekly_vault.cpp. (No
// equivalent table for Core Bosses; see the file header above for why.)
// check_weekly_vault.py (project root) validates every `targets` entry
// against events_cyclic.cpp at build time, before bump_rev runs — a typo
// here fails the build with the specific bad name, instead of silently
// never lighting up at runtime.
extern std::vector<CyclicWeeklyMapping> g_CyclicWeeklyObjectives;

// True if `eventName` (a WorldEvent::name) is a Core Boss (non-empty
// apiWorldBossId) whose name appears in some currently-live Wizard's Vault
// weekly objective title — i.e. worth auto-tracking / showing a "weekly"
// marker for at all. `outComplete` is only meaningful when this returns
// true: it reports whether that objective has already been completed this
// week. Callers (subscriptions_bar.cpp / subscriptions_window.cpp) are
// responsible for deciding what to do with a completed-but-still-manually-
// subscribed event — this function only reports the raw weekly state.
bool IsBasicEventWeeklyTarget(const std::string& eventName, bool& outComplete);

// Same as above, for one specific Cyclic Group slot (identified by group
// name + the individual CyclicGroup::Slot::name within it), matched against
// g_CyclicWeeklyObjectives.
bool IsCyclicSlotWeeklyTarget(const std::string& groupName, const std::string& slotName, bool& outComplete);
