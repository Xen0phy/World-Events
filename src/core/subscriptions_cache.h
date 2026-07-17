#pragma once
#include <string>
#include <vector>
#include <ctime>

// ---------------------------------------------------------------------------
// subscriptions_cache.h
// ---------------------------------------------------------------------------
// Shared, per-frame-cheap cache of the FULLY RESOLVED state of every
// subscribed/auto-tracked Basic Event and Cyclic slot, consulted by
// subscriptions_bar.cpp / subscriptions_window.cpp /
// subscriptions_notification.cpp instead of each independently re-deriving
// it from g_SubscribedBasicEvents/g_SubscribedCyclicSlots + g_Events/
// g_CyclicGroups + gw2_api.h + events_tracking.h + weekly_vault.h from
// scratch every single frame.
//
// This grew out of (and absorbs) an earlier, narrower cache that only
// covered weekly Wizard's Vault target status. That fixed one specific
// redundant scan, but left every other piece of per-subscription state —
// resolving a subscribed name/key to its actual WorldEvent/CyclicGroup::
// Slot, "already done today" (both the GW2-API-confirmed and the manual
// mark), chat codes, display labels — being independently re-derived by
// all three UI files, every frame, regardless of whether anything visible
// had actually changed. That's why all three views were costing roughly
// the same amount even at rest: none of that work was rendering, it was
// three redundant copies of "re-derive the whole subscription list from
// raw data."
//
// WHAT'S CACHED vs WHAT'S COMPUTED FRESH EVERY FRAME:
//   - Everything in ResolvedSubscription below (name resolution, weekly-
//     target status, doneToday, chat code/label, and the raw schedule
//     fields copied out of WorldEvent/CyclicGroup::Slot) is resolved once
//     at cache-rebuild time, not every frame.
//   - active / secsUntilStart / secsUntilEnd are the one part of a
//     subscription's state that legitimately needs to reflect "right now"
//     rather than "as of the last rebuild" — GetSubscriptionActiveState
//     computes these fresh every frame, but as pure arithmetic against the
//     fields already copied into ResolvedSubscription, with no locks, no
//     allocations, and no lookups into g_Events/g_CyclicGroups/gw2_api
//     involved at all.
//
// WHEN A REBUILD ACTUALLY HAPPENS (see RefreshSubscriptionsCache):
//   - the subscribed set itself changed (subscribe/unsubscribe/rename),
//   - a fresh GW2 API poll landed (world boss / map chest / weekly
//     objective completion may have changed),
//   - the UTC day rolled over (done-today flags reset),
//   - a "done for today" marker was toggled,
//   - the weekly reset rolled over (Monday 07:30 UTC — a new target set),
//   - or a periodic safety-net interval has elapsed (bounds staleness for
//     the one case this cache has no dedicated invalidation hook for: the
//     user editing an already-subscribed/tracked event's own schedule
//     in the options panel while it's live — a rare enough edit-while-
//     watching scenario that a bounded few seconds of staleness is an
//     accepted tradeoff rather than adding an invalidation hook to every
//     field editor in addon_options.cpp).
// Every other frame, this is a handful of integer/atomic comparisons and
// nothing else.
// ---------------------------------------------------------------------------

struct ResolvedSubscription
{
    std::string key;                  // "Basic:<name>" / "Cyclic:<group>:<offset>" — same convention used throughout the three UI files
    bool        isBasic = true;
    std::string basicName;            // valid when isBasic
    std::string cyclicGroupName;      // valid when !isBasic
    int         cyclicSlotOffset = 0; // valid when !isBasic

    std::string label;    // display name, e.g. "Tequatl the Sunless" or "Domain of Vabbi - Forged Assault"
    std::string chatCode;

    bool manuallySubscribed = false; // false => present only via weekly auto-track (WeeklyAutoTrackEnabled)
    bool isWeeklyTarget     = false; // active-and-incomplete weekly Wizard's Vault target this week (weekly_vault.h)
    bool doneToday          = false; // API-confirmed OR manually marked done. All three views (bar, window,
                                      // notifications) skip a subscription once this is true — a done event/slot
                                      // doesn't need to be surfaced anywhere until the next reset. Kept as a plain
                                      // data field here (rather than pre-filtered out of the resolved list) only
                                      // because it can change without a rebuild being otherwise necessary and each
                                      // view applies the skip itself at consumption time.

    // Copied out of WorldEvent/CyclicGroup::Slot at resolve time so
    // GetSubscriptionActiveState never needs to re-resolve into
    // g_Events/g_CyclicGroups (which the options panel can add to/remove
    // from at runtime — see the safety-net note above for the one gap
    // this doesn't fully close) or touch a string, at all.
    bool              isVarying = false;
    std::vector<int>  varyingTimes; // Basic + isVarying only
    int period   = 0; // Basic (non-varying): the event's own period. Cyclic: the group's period.
    int duration = 0;
    int offset   = 0; // Basic (non-varying): ev.offset. Cyclic: slot.offset.
    int repeat   = 1; // Cyclic only
};

struct SubscriptionActiveState
{
    bool active = false;
    int  secsUntilStart = -1; // 0 if active; -1 if no usable schedule data
    int  secsUntilEnd    = -1; // only meaningful if active; -1 otherwise
};

// Call once per frame from each of RenderSubscriptionsBar/Window/
// Notifications — safe and cheap to call from all three every frame —
// before consulting GetResolvedSubscriptions/GetSubscriptionActiveState
// below. See the file header above for exactly when a real rebuild
// happens; every other frame this is a handful of comparisons and nothing
// else.
void RefreshSubscriptionsCache(time_t now);

// The resolved list as of the last RefreshSubscriptionsCache call: EVERY
// manually-subscribed item (regardless of doneToday, since doneToday can
// flip without a rebuild happening first) PLUS every active-and-incomplete
// weekly auto-track target not already manually subscribed (only present
// when WeeklyAutoTrackEnabled). All three UI files apply the same doneToday
// skip on top of this shared list — see ResolvedSubscription::doneToday.
const std::vector<ResolvedSubscription>& GetResolvedSubscriptions();

// Pure arithmetic against ResolvedSubscription's copied-out schedule
// fields — no locks, no allocations, no lookups — so it's fine (and
// necessary, since this is the one continuously-changing part of a
// subscription's state) to call this fresh every frame for every item.
SubscriptionActiveState GetSubscriptionActiveState(const ResolvedSubscription& sub, time_t now);
