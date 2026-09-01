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
// subscriptions_bar.cpp/subscriptions_window.cpp/subscriptions_notification.cpp.
//
// WHAT'S CACHED VS COMPUTED FRESH EVERY FRAME: everything in ResolvedSubscription
// is resolved once at cache-rebuild time. active/secsUntilStart/secsUntilEnd
// instead reflect "right now": GetSubscriptionActiveState computes these as
// pure arithmetic against the fields already copied into ResolvedSubscription,
// with no locks, allocations, or lookups into g_Events/g_CyclicGroups/gw2_api
// involved.
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
// doneToday            API-confirmed (Gw2ApiAutoMarkDoneEnabled) OR manually
//                      marked done
// isVarying            per-day (Basic) or per-cycle (Cyclic) time list
//                      instead of a fixed period / offset+repeat
// varyingTimes         isVarying only: sorted times within the relevant
//                      cycle (a day for Basic, the group's period for Cyclic)
// period               Basic (non-varying): the event's own period.
//                      Cyclic: the group's period
// duration             event/slot duration in seconds
// offset               Basic (non-varying): ev.offset. Cyclic (non-varying):
//                      slot.offset
// repeat               Cyclic (non-varying) only: how many times the slot
//                      recurs per period
//--------------------------------------------------------------------------------
// isVarying/varyingTimes/period/duration/offset/repeat are copied out of
// WorldEvent/CyclicGroup::Slot at resolve time, so GetSubscriptionActiveState
// never needs to re-resolve into g_Events/g_CyclicGroups (which the options panel
// can mutate at runtime - see the file header's safety-net note for the one gap
// this doesn't fully close) or touch a string.
//
// doneToday is kept as a plain field here, because it can change without a
// rebuild. Each of the three views applies the skip itself at consumption time.
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
// Call RefreshSubscriptionsCache once per frame, before the other two; see the
// file header for when a rebuild actually happens (usually just a handful of
// comparisons).
//
// GetResolvedSubscriptions returns the list as of the last refresh: every
// manually-subscribed item plus every active-and-incomplete weekly auto-track
// target not already subscribed; callers apply the doneToday skip themselves.
// GetSubscriptionActiveState is pure arithmetic against the already-copied
// schedule fields, safe to call fresh every frame for every item.
//--------------------------------------------------------------------------------
void RefreshSubscriptionsCache(time_t now);
const std::vector<ResolvedSubscription>& GetResolvedSubscriptions();
SubscriptionActiveState GetSubscriptionActiveState(const ResolvedSubscription& sub, time_t now);