//################################################################################
// subscriptions_cache.h
//--------------------------------------------------------------------------------
// ResolvedSubscription                  fully-resolved per-subscription
//                                        state, rebuilt on demand
// SubscriptionActiveState                active/secsUntilStart/secsUntilEnd,
//                                        computed fresh every frame
// RefreshSubscriptionsCache /            rebuild-if-needed entry point, plus
// GetResolvedSubscriptions /             the two accessors all three UI
// GetSubscriptionActiveState             files consume
//--------------------------------------------------------------------------------
// Shared, per-frame-cheap cache of the fully resolved state of every
// subscribed/auto-tracked Basic Event and Cyclic slot, replacing three
// independent per-frame re-derivations previously done separately by
// subscriptions_bar.cpp/subscriptions_window.cpp/
// subscriptions_notification.cpp from g_SubscribedBasicEvents/
// g_SubscribedCyclicSlots + g_Events/g_CyclicGroups + gw2_api.h +
// events_tracking.h + weekly_vault.h.
//
// WHAT'S CACHED vs COMPUTED FRESH EVERY FRAME:
// Everything in ResolvedSubscription is resolved once at cache-rebuild
// time. active/secsUntilStart/secsUntilEnd are the one part that must
// reflect "right now" rather than "as of the last rebuild" -
// GetSubscriptionActiveState computes these as pure arithmetic against the
// fields already copied into ResolvedSubscription, with no locks,
// allocations, or lookups into g_Events/g_CyclicGroups/gw2_api involved.
//
// WHEN A REBUILD ACTUALLY HAPPENS (see RefreshSubscriptionsCache):
// the subscribed set changed (subscribe/unsubscribe/rename), a fresh GW2
// API poll landed, the UTC day rolled over, a done-today marker was
// toggled, the weekly reset rolled over (Monday 07:30 UTC), or a periodic
// safety-net interval elapsed. The safety net bounds staleness for the one
// gap with no dedicated invalidation hook: editing an already-subscribed
// event's own schedule in the options panel while it's live - a rare
// edit-while-watching case where a bounded few seconds of staleness is an
// accepted tradeoff over adding a hook to every field editor in
// addon_options.cpp.
//
// Every other frame, this is a handful of integer/atomic comparisons and
// nothing else.
//--------------------------------------------------------------------------------

#pragma once

#include <ctime>
#include <string>
#include <vector>

//********************************************************************************
// ResolvedSubscription
//--------------------------------------------------------------------------------
// key                 "Basic:<name>" / "Cyclic:<group>:<offset>" - same
//                      convention used throughout the three UI files
// isBasic              true = Basic Event, false = Cyclic slot
// basicName            valid when isBasic
// cyclicGroupName      valid when !isBasic
// cyclicSlotOffset     valid when !isBasic
// label                display name, e.g. "Tequatl the Sunless" or
//                      "Domain of Vabbi - Forged Assault"
// chatCode             map-travel chat code
// manuallySubscribed   false => present only via weekly auto-track
//                      (WeeklyAutoTrackEnabled)
// isWeeklyTarget       active-and-incomplete weekly Wizard's Vault target
//                      this week (weekly_vault.h)
// doneToday            API-confirmed OR manually marked done
// isVarying            Basic Events with a per-day time list instead of a
//                      fixed period
// varyingTimes         Basic + isVarying only
// period               Basic (non-varying): the event's own period.
//                      Cyclic: the group's period
// duration             event/slot duration in seconds
// offset               Basic (non-varying): ev.offset. Cyclic: slot.offset
// repeat               Cyclic only: how many times the slot recurs per
//                      period
//--------------------------------------------------------------------------------
// isVarying/varyingTimes/period/duration/offset/repeat are copied out of
// WorldEvent/CyclicGroup::Slot at resolve time, so GetSubscriptionActiveState
// never needs to re-resolve into g_Events/g_CyclicGroups (which the options
// panel can mutate at runtime - see the file header's safety-net note for
// the one gap this doesn't fully close) or touch a string.
//
// doneToday is kept as a plain field here, rather than pre-filtered out of
// the resolved list, because it can change without a rebuild - each of the
// three views applies the skip itself at consumption time.
//--------------------------------------------------------------------------------
struct ResolvedSubscription
{
    std::string key;
    bool        isBasic = true;
    std::string basicName;
    std::string cyclicGroupName;
    int         cyclicSlotOffset = 0;

    std::string label;
    std::string chatCode;

    bool manuallySubscribed = false;
    bool isWeeklyTarget     = false;
    bool doneToday          = false;

    bool              isVarying = false;
    std::vector<int>  varyingTimes;
    int period   = 0;
    int duration = 0;
    int offset   = 0;
    int repeat   = 1;
};

//********************************************************************************
// SubscriptionActiveState
//--------------------------------------------------------------------------------
// active           currently within its active window
// secsUntilStart   0 if active; -1 if no usable schedule data
// secsUntilEnd     only meaningful if active; -1 otherwise
//--------------------------------------------------------------------------------
struct SubscriptionActiveState
{
    bool active = false;
    int  secsUntilStart = -1;
    int  secsUntilEnd   = -1;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RefreshSubscriptionsCache / GetResolvedSubscriptions /
// GetSubscriptionActiveState
//--------------------------------------------------------------------------------
// Call RefreshSubscriptionsCache once per frame, before the other two -
// safe and cheap every frame; see the file header for when a real rebuild
// actually happens (usually just a handful of comparisons).
//
// GetResolvedSubscriptions returns the list as of the last refresh: every
// manually-subscribed item plus every active-and-incomplete weekly
// auto-track target not already subscribed. Callers apply the doneToday
// skip themselves.
//
// GetSubscriptionActiveState is pure arithmetic against the already-copied
// schedule fields - safe to call fresh every frame for every item.
//--------------------------------------------------------------------------------
void RefreshSubscriptionsCache(time_t now);
const std::vector<ResolvedSubscription>& GetResolvedSubscriptions();
SubscriptionActiveState GetSubscriptionActiveState(const ResolvedSubscription& sub, time_t now);