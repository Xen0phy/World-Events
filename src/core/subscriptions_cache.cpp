//################################################################################
// subscriptions_cache.cpp
//--------------------------------------------------------------------------------
// See subscriptions_cache.h for the overall design/rationale.
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_tracking.h"
#include "gw2_api.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_cache.h"
#include "weekly_vault.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

//********************************************************************************
// WeeklyTargetInfo
//--------------------------------------------------------------------------------
// mappingTitle   best-effort label for the matched weekly objective
// complete       whether that objective is already done this week
//--------------------------------------------------------------------------------
// Private to this file: which mapping (if any) a Basic Event/Cyclic slot
// counts toward this week, and whether it's complete. Only used while
// building s_resolved - callers outside this file only ever need
// ResolvedSubscription::isWeeklyTarget, never the raw mapping data.
//--------------------------------------------------------------------------------
namespace
{
    struct WeeklyTargetInfo
    {
        std::string mappingTitle;
        bool        complete = false;
    };
}

static std::unordered_map<std::string, WeeklyTargetInfo> s_weeklyCache;   //. key: "Basic:<name>" / "Cyclic:<group>:<offset>"
static std::vector<ResolvedSubscription>                 s_resolved;

//_ Recorded after each rebuild; compared against current values in
// RefreshSubscriptionsCache's needRebuild check below.
static bool     s_cacheEverBuilt = false;
static time_t   s_cacheBuiltForResetEpoch   = 0;
static uint64_t s_lastAppliedFetchGeneration = 0;
static uint64_t s_lastSubscriptionGeneration = 0;
static uint64_t s_lastDoneMarkerGeneration   = 0;
static long long s_lastUtcDay = -1;

//_ Bounds staleness for the one invalidation gap this cache has no
// dedicated hook for - see the file header's safety-net note.
static constexpr double kSafetyNetRebuildSeconds = 10.0;
static time_t s_lastRebuildWallClock = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetCurrentWeeklyResetEpoch
//--------------------------------------------------------------------------------
// Weekly Wizard's Vault objectives reset every Monday at 07:30 UTC. Returns
// the epoch time of the most recent such reset at or before `now` - i.e.
// the start of the CURRENT weekly period, so two `now` values in the same
// weekly period always compare equal.
//--------------------------------------------------------------------------------
static time_t GetCurrentWeeklyResetEpoch(time_t now)
{
    tm utc{};
    //_ MSVC-style signature (buffer first, time second); MinGW-w64
    // provides this by default.
    gmtime_s(&utc, &now);

    //_ tm_wday: Sunday = 0..Saturday = 6. Remap so Monday = 0..Sunday = 6,
    // i.e. "how many days since this week's Monday".
    int daysSinceMonday = (utc.tm_wday + 6) % 7;

    tm reset = utc;
    reset.tm_mday -= daysSinceMonday;
    reset.tm_hour = 7;
    reset.tm_min  = 30;
    reset.tm_sec  = 0;
    //_ MSVC/MinGW UTC equivalent of mktime; normalizes an out-of-range
    // tm_mday correctly.
    time_t resetTime = _mkgmtime(&reset);

    //_ This week's Monday 07:30 hasn't happened yet - the current period
    // started last week instead.
    if (resetTime > now)
        resetTime -= 7 * 24 * 3600;

    return resetTime;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RebuildWeeklyCache
//--------------------------------------------------------------------------------
// Populates s_weeklyCache by asking weekly_vault.h's own matching functions
// (IsBasicEventWeeklyTarget/IsCyclicSlotWeeklyTarget) which Basic Events/
// Cyclic slots are live weekly targets, rather than duplicating that logic
// here. Basic: every Core Boss is checked, the whole candidate set. Cyclic:
// only slots referenced by g_CyclicWeeklyObjectives are walked, resolving
// each target's slot NAME to the stable slot OFFSET against g_CyclicGroups.
// Only called from a full rebuild, never per-frame.
//--------------------------------------------------------------------------------
static void RebuildWeeklyCache()
{
    s_weeklyCache.clear();

    for (const auto& ev : g_Events)
    {
        if (ev.apiWorldBossId.empty()) continue;   //. not a Core Boss

        WeeklyTargetInfo info;
        if (!IsBasicEventWeeklyTarget(ev.name, info.complete)) continue;

        info.mappingTitle = ev.name;   //. no separate mapping object
        s_weeklyCache["Basic:" + ev.name] = info;
    }

    for (const auto& mapping : g_CyclicWeeklyObjectives)
    {
        for (const auto& target : mapping.targets)
        {
            WeeklyTargetInfo info;
            if (!IsCyclicSlotWeeklyTarget(target.groupName, target.slotName, info.complete)) continue;

            auto grpIt = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
                [&](const CyclicGroup& g) { return g.name == target.groupName; });
            if (grpIt == g_CyclicGroups.end()) continue;   //. group renamed or deleted

            auto slotIt = std::find_if(grpIt->slots.begin(), grpIt->slots.end(),
                [&](const CyclicGroup::Slot& s) { return s.name == target.slotName; });
            if (slotIt == grpIt->slots.end()) continue;   //. slot renamed or deleted

            char offsetBuf[16];
            snprintf(offsetBuf, sizeof(offsetBuf), "%d", slotIt->offset);

            info.mappingTitle = target.groupName + " - " + target.slotName;   //. internal-only label
            s_weeklyCache["Cyclic:" + grpIt->name + ":" + offsetBuf] = info;
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResolveBasic / ResolveCyclic
//--------------------------------------------------------------------------------
// Build one ResolvedSubscription from a WorldEvent / (CyclicGroup, Slot)
// pair - the one place doneToday/isWeeklyTarget/schedule-field-copying
// logic lives, shared by the manual-subscription pass and the auto-tracked
// weekly pass below (mirrors how subscriptions_bar.cpp's AddBasicSegment/
// AddCyclicSegment split their own equivalent).
//--------------------------------------------------------------------------------
static ResolvedSubscription ResolveBasic(const WorldEvent& ev, bool manuallySubscribed)
{
    ResolvedSubscription r;
    r.key                = "Basic:" + ev.name;
    r.isBasic            = true;
    r.basicName          = ev.name;
    r.label              = ev.name;
    r.chatCode           = ev.chatCode;
    r.manuallySubscribed = manuallySubscribed;

    if (auto it = s_weeklyCache.find(r.key); it != s_weeklyCache.end())
        r.isWeeklyTarget = !it->second.complete;

    bool apiDone    = !ev.apiWorldBossId.empty() && IsWorldBossCompletedToday(ev.apiWorldBossId);
    bool manualDone = IsBasicEventMarkedDoneToday(ev.name);
    r.doneToday = apiDone || manualDone;

    r.isVarying    = ev.isVarying;
    r.varyingTimes = ev.varyingTimes;
    r.period       = ev.period;
    r.duration     = ev.duration;
    r.offset       = ev.offset;
    r.repeat       = 1;

    return r;
}

static ResolvedSubscription ResolveCyclic(const CyclicGroup& grp, const CyclicGroup::Slot& slot, bool manuallySubscribed)
{
    char offsetBuf[16];
    snprintf(offsetBuf, sizeof(offsetBuf), "%d", slot.offset);

    ResolvedSubscription r;
    r.key                = "Cyclic:" + grp.name + ":" + offsetBuf;
    r.isBasic            = false;
    r.cyclicGroupName    = grp.name;
    r.cyclicSlotOffset   = slot.offset;
    r.label              = grp.name + " - " + slot.name;
    r.chatCode           = slot.chatCode;
    r.manuallySubscribed = manuallySubscribed;

    if (auto it = s_weeklyCache.find(r.key); it != s_weeklyCache.end())
        r.isWeeklyTarget = !it->second.complete;

    bool apiDone    = !grp.apiMapChestId.empty() && IsMapChestClaimedToday(grp.apiMapChestId);
    bool manualDone = IsCyclicSlotMarkedDoneToday({ grp.name, slot.offset });
    r.doneToday = apiDone || manualDone;

    r.isVarying    = slot.isVarying;
    r.varyingTimes = slot.varyingTimes;
    r.period       = grp.period;
    r.duration     = slot.duration;
    r.offset       = slot.offset;
    r.repeat       = slot.repeat > 0 ? slot.repeat : 1;

    return r;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RebuildResolvedSubscriptions
//--------------------------------------------------------------------------------
// The one place that walks g_SubscribedBasicEvents/g_SubscribedCyclicSlots
// and (when WeeklyAutoTrackEnabled) s_weeklyCache to build the full
// resolved list - called once per rebuild, shared by all three UI files,
// instead of three independent copies of this same walk every frame.
//--------------------------------------------------------------------------------
static void RebuildResolvedSubscriptions()
{
    s_resolved.clear();

    //_ Manually subscribed Basic Events.
    for (const auto& evName : g_SubscribedBasicEvents)
    {
        auto it = std::find_if(g_Events.begin(), g_Events.end(),
            [&](const WorldEvent& ev) { return ev.name == evName; });
        if (it == g_Events.end()) continue;   //. deleted since subscribing

        s_resolved.push_back(ResolveBasic(*it, true));
    }

    //_ Manually subscribed Cyclic slots.
    for (const auto& subKey : g_SubscribedCyclicSlots)
    {
        auto grpIt = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
            [&](const CyclicGroup& grp) { return grp.name == subKey.groupName; });
        if (grpIt == g_CyclicGroups.end()) continue;   //. group deleted since subscribing

        auto slotIt = std::find_if(grpIt->slots.begin(), grpIt->slots.end(),
            [&](const CyclicGroup::Slot& s) { return s.offset == subKey.slotOffset; });
        if (slotIt == grpIt->slots.end()) continue;   //. slot deleted since subscribing

        s_resolved.push_back(ResolveCyclic(*grpIt, *slotIt, true));
    }

    //_ Auto-tracked weekly targets not already manually subscribed
    // (WeeklyAutoTrackEnabled); iterates the small weekly cache instead
    // of every WorldEvent/CyclicGroup::Slot.
    if (WeeklyAutoTrackEnabled)
    {
        for (const auto& kv : s_weeklyCache)
        {
            //_ Copied out since pre-C++20 lambdas can't capture structured
            // bindings (kv.first/kv.second) directly.
            const std::string&      cacheKey = kv.first;
            const WeeklyTargetInfo& info     = kv.second;
            if (info.complete) continue;

            bool alreadyResolved = std::find_if(s_resolved.begin(), s_resolved.end(),
                [&](const ResolvedSubscription& r) { return r.key == cacheKey; }) != s_resolved.end();
            if (alreadyResolved) continue;   //. already added manually

            if (cacheKey.rfind("Basic:", 0) == 0)
            {
                std::string name = cacheKey.substr(6);
                auto evIt = std::find_if(g_Events.begin(), g_Events.end(),
                    [&](const WorldEvent& e) { return e.name == name; });
                if (evIt == g_Events.end()) continue;   //. event renamed or deleted

                s_resolved.push_back(ResolveBasic(*evIt, false));
            }
            else
            {
                //_ "Cyclic:<group>:<offset>" - split on the LAST ':' since
                // a group name could itself contain one.
                size_t lastColon = cacheKey.rfind(':');
                std::string groupName = cacheKey.substr(7, lastColon - 7);   //. 7 == strlen("Cyclic:")
                int offset = atoi(cacheKey.c_str() + lastColon + 1);

                auto grpIt = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
                    [&](const CyclicGroup& g) { return g.name == groupName; });
                if (grpIt == g_CyclicGroups.end()) continue;   //. group renamed or deleted

                auto slotIt = std::find_if(grpIt->slots.begin(), grpIt->slots.end(),
                    [&](const CyclicGroup::Slot& s) { return s.offset == offset; });
                if (slotIt == grpIt->slots.end()) continue;   //. slot renamed or deleted

                s_resolved.push_back(ResolveCyclic(*grpIt, *slotIt, false));
            }
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RefreshSubscriptionsCache / GetResolvedSubscriptions
//--------------------------------------------------------------------------------
void RefreshSubscriptionsCache(time_t now)
{
    time_t    resetEpoch = GetCurrentWeeklyResetEpoch(now);
    long long utcDay     = (long long)(now / 86400);
    uint64_t  subGen     = GetSubscriptionListGeneration();
    uint64_t  doneGen    = GetDoneMarkersGeneration();
    uint64_t  fetchGen   = GetGw2ApiFetchGeneration();

    bool needRebuild =
        !s_cacheEverBuilt ||
        resetEpoch != s_cacheBuiltForResetEpoch   ||
        subGen     != s_lastSubscriptionGeneration ||
        utcDay     != s_lastUtcDay                 ||
        doneGen    != s_lastDoneMarkerGeneration   ||
        fetchGen   != s_lastAppliedFetchGeneration ||
        (now - s_lastRebuildWallClock) >= (time_t)kSafetyNetRebuildSeconds;

    if (!needRebuild) return;

    RebuildWeeklyCache();
    RebuildResolvedSubscriptions();

    s_cacheBuiltForResetEpoch    = resetEpoch;
    s_lastSubscriptionGeneration = subGen;
    s_lastUtcDay                 = utcDay;
    s_lastDoneMarkerGeneration   = doneGen;
    s_lastAppliedFetchGeneration = fetchGen;
    s_lastRebuildWallClock       = now;
    s_cacheEverBuilt = true;
}

const std::vector<ResolvedSubscription>& GetResolvedSubscriptions()
{
    return s_resolved;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetSubscriptionActiveState
//--------------------------------------------------------------------------------
// Four branches: varying-schedule Basic Events (walk the sorted start
// times), periodic (non-varying) Basic Events, varying-schedule Cyclic
// slots (same walk, scoped to the group's period instead of a day), and
// periodic (non-varying) Cyclic slots (checks every repeat within the
// period, since a slot can recur more than once). All four return early
// once an active occurrence is found.
//--------------------------------------------------------------------------------
SubscriptionActiveState GetSubscriptionActiveState(const ResolvedSubscription& sub, time_t now)
{
    SubscriptionActiveState st;

    if (sub.isBasic && sub.isVarying)
    {
        if (sub.varyingTimes.empty()) return st;   //. no schedule data

        int secondsOfDay = (int)(now % 86400);
        for (int t : sub.varyingTimes)
        {
            if (secondsOfDay < t) { st.secsUntilStart = t - secondsOfDay; return st; }   //. hasn't started yet
            if (secondsOfDay < t + sub.duration)
            {
                st.active         = true;
                st.secsUntilStart = 0;
                st.secsUntilEnd   = t + sub.duration - secondsOfDay;
                return st;
            }
            //_ else: already passed today - check the next scheduled time.
        }
        //_ All times passed today - wrap to the first one tomorrow.
        st.secsUntilStart = 86400 - secondsOfDay + sub.varyingTimes[0];
        return st;
    }

    if (sub.isBasic)
    {
        //_ Periodic (non-varying) Basic Event.
        if (sub.period <= 0) return st;   //. no schedule data

        int phase = (((int)(now % sub.period) - sub.offset) % sub.period + sub.period) % sub.period;
        if (phase < sub.duration)
        {
            st.active         = true;
            st.secsUntilStart = 0;
            st.secsUntilEnd   = sub.duration - phase;
        }
        else
        {
            st.secsUntilStart = sub.period - phase;
        }
        return st;
    }

    if (!sub.isBasic && sub.isVarying)
    {
        if (sub.varyingTimes.empty() || sub.period <= 0) return st;   //. no schedule data

        int secondsOfPeriod = (int)(now % sub.period);
        for (int t : sub.varyingTimes)
        {
            if (secondsOfPeriod < t) { st.secsUntilStart = t - secondsOfPeriod; return st; }   //. hasn't started yet this cycle
            if (secondsOfPeriod < t + sub.duration)
            {
                st.active         = true;
                st.secsUntilStart = 0;
                st.secsUntilEnd   = t + sub.duration - secondsOfPeriod;
                return st;
            }
            //_ else: already passed this cycle - check the next scheduled time.
        }
        //_ All times passed this cycle - wrap to the first one next cycle.
        st.secsUntilStart = sub.period - secondsOfPeriod + sub.varyingTimes[0];
        return st;
    }

    //_ Previously computed independently by AddCyclicSegment/
    // GetCyclicSlotStatus/AddCyclicCandidate.
    if (sub.period <= 0) return st;   //. no schedule data

    int secondsOfDay = (int)(now % sub.period);
    int subSpan      = sub.period / sub.repeat;
    int bestSecsUntil = sub.period;

    for (int r = 0; r < sub.repeat; r++)
    {
        int baseOffset     = sub.offset + r * subSpan;
        int phase          = ((secondsOfDay - baseOffset) % sub.period + sub.period) % sub.period;
        bool slotActive    = (phase < sub.duration);
        int secsUntilStart = slotActive ? 0 : (sub.period - phase);

        if (slotActive)
        {
            st.active         = true;
            st.secsUntilStart = 0;
            st.secsUntilEnd   = sub.duration - phase;
            return st;   //. only one active repeat
        }
        if (secsUntilStart < bestSecsUntil)
            bestSecsUntil = secsUntilStart;
    }

    st.secsUntilStart = bestSecsUntil;
    return st;
}