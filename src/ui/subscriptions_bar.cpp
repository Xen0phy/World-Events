// subscriptions_bar.cpp
// Draws the "Subscriptions" distribution line: a thin overlay pinned to the
// top (or bottom) edge of the screen, with one colored segment per
// subscribed event/slot across a fixed 2h window, that curves into a
// filled colored block under the mouse.

#include "subscriptions.h"
#include "events_tracking.h"
#include "events.h"
#include "maprender.h"
#include "settings.h"
#include "gw2_api.h"
#include "weekly_vault.h"
#include "imgui.h"
#include <ctime>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>
#include <unordered_map>
#include <climits>
#include <algorithm>

// The line always represents exactly this much time, starting at "now".
// Local x-coordinate 0..W along the strip maps linearly to 0..kWindowSeconds.
static constexpr int kWindowSeconds = 2 * 60 * 60; // 2 hours

// Deterministic color for a Basic Event, derived from its name (FNV-1a hash
// -> hue), since Basic Events don't carry a color of their own.
static ImU32 BasicEventColorFor(const std::string& name)
{
    unsigned int hash = 2166136261u; // FNV-1a 32-bit offset basis
    for (unsigned char c : name)
    {
        hash ^= c;
        hash *= 16777619u;
    }

    float hue = (hash % 360) / 360.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

// One drawable segment on the strip, in local pixel space (0..W).
struct LineSegment
{
    std::string key;          // stable identity, e.g. "Basic:Name" or "Cyclic:Group:Offset"
    std::string name;         // display name for tooltip
    std::string chatCode;
    float       startX;       // clamped to [0, W]
    float       endX;         // > startX, clamped to [0, W]
    bool        active;
    int         statusSecs;   // secs left if active, secs until start otherwise
    int         durationSecs; // endSec - startSec, clamped to the window; used for widest-first stack ordering
    ImU32       color;
    int         lane = 0;     // 0 = drawn on the resting baseline; >0 = hidden behind lane 0, shown only via dot marker + hover
    bool        isWeekly = false; // true = an active-and-incomplete weekly Wizard's Vault target this week (weekly_vault.h) — draws an additional small red marker, independent of everything else in this struct

    // Identity for the right-click "Mark done for today" menu — see the
    // click hit-testing block near the end of this file. Mirrors the
    // isBasic/basicName/cyclicKey trio in subscriptions_window.cpp's Row.
    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

// Builds the second label line: "Active - ends in Xm YYs" or "in Xm YYs".
static std::string SegmentStatusLine(const LineSegment& seg)
{
    char buf[48];
    if (seg.active)
        snprintf(buf, sizeof(buf), "Active - ends in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    else
        snprintf(buf, sizeof(buf), "in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    return std::string(buf);
}

// Greedy interval-graph coloring: walks segments left-to-right (already
// sorted by startX) and assigns each the lowest-numbered lane whose last
// occupant has already ended, so only one segment per point in time draws
// on the resting baseline (lane 0); everything else (lane > 0) is only
// surfaced via a dot marker + hover.
static void AssignLanes(std::vector<LineSegment>& segs)
{
    constexpr float kLaneMargin = 1.0f; // px of overlap tolerance
    std::vector<float> laneEndX; // laneEndX[lane] = endX of the last segment placed in that lane

    for (auto& seg : segs)
    {
        int chosenLane = -1;
        for (int lane = 0; lane < (int)laneEndX.size(); lane++)
        {
            if (seg.startX >= laneEndX[lane] - kLaneMargin) { chosenLane = lane; break; }
        }
        if (chosenLane < 0)
        {
            chosenLane = (int)laneEndX.size();
            laneEndX.push_back(0.0f);
        }
        seg.lane = chosenLane;
        laneEndX[chosenLane] = seg.endX;
    }
}

// One marker on the baseline for a hidden (lane>0) event's start tick.
struct DotMark
{
    float x;        // baseline x position (before draw-time nudging for ties)
    int   segIndex; // index into the segs vector this dot represents
};

// Builds one dot per lane>0 (hidden) segment whose start tick falls inside
// the range of whatever lane-0 segment currently occupies the baseline at
// that x. A hidden segment starting in a pure gap gets no dot.
// Additionally includes one dot per lane-0 *weekly* segment at its own
// start tick — lane-0 segments are normally represented by the colored
// baseline line alone with no dot, but a weekly segment needs a dot
// regardless of lane so its red marker (see the recoloring at the actual
// draw site below) has somewhere to render.
static std::vector<DotMark> CollectOverlapDots(const std::vector<LineSegment>& segs, float stripWidth)
{
    std::vector<DotMark> dots;

    std::vector<int> lane0Indices;
    for (int i = 0; i < (int)segs.size(); i++)
        if (segs[i].lane == 0) lane0Indices.push_back(i);

    for (int i = 0; i < (int)segs.size(); i++)
    {
        if (segs[i].lane == 0)
        {
            if (segs[i].isWeekly) dots.push_back({ segs[i].startX, i });
            continue;
        }

        float startX = segs[i].startX;
        for (int lane0Idx : lane0Indices)
        {
            const LineSegment& shown = segs[lane0Idx];
            if (startX >= shown.startX && startX < shown.endX)
            {
                dots.push_back({ startX, i });
                break;
            }
        }
    }

    // Sort by x so exact-tie ticks group together for the nudge-apart pass at draw time.
    std::sort(dots.begin(), dots.end(), [](const DotMark& a, const DotMark& b) { return a.x < b.x; });

    return dots;
}

// Minimal-mode counterpart to CollectOverlapDots: every segment (lane 0
// included) gets one dot at its own start tick, since minimal mode has no
// colored baseline line to represent lane-0 segments instead.
static std::vector<DotMark> CollectAllEventDots(const std::vector<LineSegment>& segs)
{
    std::vector<DotMark> dots;
    dots.reserve(segs.size());
    for (int i = 0; i < (int)segs.size(); i++)
        dots.push_back({ segs[i].startX, i });

    std::sort(dots.begin(), dots.end(), [](const DotMark& a, const DotMark& b) { return a.x < b.x; });

    return dots;
}

// Whether a segment's x-range overlaps either configured "unsafe" margin
// (GW2's own corner UI), so its drop can start further down instead of
// covering that UI. A margin of 0 disables that side's zone.
static bool SegmentOverlapsUnsafeZone(const LineSegment& seg, float screenW)
{
    float leftZoneEnd    = (float)std::max(0, SubscriptionsBarUnsafeLeftPx);
    float rightZoneStart = screenW - (float)std::max(0, SubscriptionsBarUnsafeRightPx);

    bool inLeftZone  = leftZoneEnd  > 0.0f && seg.startX < leftZoneEnd;
    bool inRightZone = rightZoneStart < screenW && seg.endX > rightZoneStart;

    return inLeftZone || inRightZone;
}

// Walks g_SubscribedBasicEvents / g_SubscribedCyclicSlots and produces one
// LineSegment per occurrence that overlaps the next kWindowSeconds, mapped
// into local pixel space across the given strip width.
static std::vector<LineSegment> CollectVisibleSegments(time_t now, float stripWidth)
{
    std::vector<LineSegment> segs;
    segs.reserve(g_SubscribedBasicEvents.size() + g_SubscribedCyclicSlots.size());

    auto secToX = [&](int sec) { return (sec / (float)kWindowSeconds) * stripWidth; };

    // ---- Basic Events ----
    // Factored out of the loop below so the exact same segment-building
    // logic (timing math, daily apiWorldBossId check, push_back) serves
    // both the manual-subscription pass and the weekly-auto-track pass
    // further down, differing only in the isWeekly flag they pass in.
    auto AddBasicSegment = [&](const WorldEvent& ev, bool isWeekly)
    {
        // Same "already done today" check as the watchlist window — see
        // events.h's apiWorldBossId and gw2_api.h. No-op for every event
        // other than the 13 Core Bosses. Entirely independent of the
        // isWeekly/weekly Wizard's Vault check below — different reward
        // track, different reset schedule (see weekly_vault.h).
        if (!ev.apiWorldBossId.empty() && IsWorldBossCompletedToday(ev.apiWorldBossId))
            return;

        // Manual counterpart to the API check above — seeevents_tracking.h.
        if (IsBasicEventMarkedDoneToday(ev.name))
            return;

        bool active = IsEventActive(ev, now);
        int  startSec, endSec, statusSecs;

        if (active)
        {
            int secsLeft = GetSecondsUntilEventEnd(ev, now);
            if (secsLeft < 0) return; // no timer data
            startSec   = 0; // already underway
            endSec     = std::min(secsLeft, kWindowSeconds);
            statusSecs = secsLeft;
        }
        else
        {
            int secsUntilStart = GetSecondsUntilEventStart(ev, now);
            if (secsUntilStart < 0 || secsUntilStart >= kWindowSeconds) return;
            startSec   = secsUntilStart;
            endSec     = std::min(secsUntilStart + ev.duration, kWindowSeconds);
            statusSecs = secsUntilStart;
        }

        if (endSec <= startSec) return;
        if (active && SubscriptionsBarHideActive) return;

        LineSegment seg{
            "Basic:" + ev.name, ev.name, ev.chatCode,
            secToX(startSec), secToX(endSec), active, statusSecs, endSec - startSec,
            BasicEventColorFor(ev.name)
        };
        seg.isWeekly  = isWeekly;
        seg.isBasic   = true;
        seg.basicName = ev.name;
        segs.push_back(seg);
    };

    for (const auto& evName : g_SubscribedBasicEvents)
    {
        auto it = std::find_if(g_Events.begin(), g_Events.end(),
            [&](const WorldEvent& ev) { return ev.name == evName; });
        if (it == g_Events.end()) continue; // deleted since subscribing

        bool weeklyComplete = false;
        bool isWeekly = IsBasicEventWeeklyTarget(it->name, weeklyComplete) && !weeklyComplete;
        AddBasicSegment(*it, isWeekly);
    }

    // Auto-tracked: NOT manually subscribed, but an active-and-incomplete
    // weekly Wizard's Vault target this week — see weekly_vault.cpp for
    // where to adjust which events count toward which objective. Drops
    // off again on its own the moment the objective completes, same as
    // any other weekly target; a manually-subscribed one (handled by the
    // loop just above) stays regardless. Gated by WeeklyAutoTrackEnabled
    // (settings_table.h) — the master on/off for this auto-add behavior,
    // shared with subscriptions_window.cpp/subscriptions_notification.cpp.
    if (WeeklyAutoTrackEnabled)
    {
        for (const auto& ev : g_Events)
        {
            bool alreadyManual = std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), ev.name) != g_SubscribedBasicEvents.end();
            if (alreadyManual) continue; // already handled above

            bool weeklyComplete = false;
            if (!IsBasicEventWeeklyTarget(ev.name, weeklyComplete)) continue;
            if (weeklyComplete) continue;

            AddBasicSegment(ev, true);
        }
    }

    // ---- Cyclic Events ----
    auto AddCyclicSegment = [&](const CyclicGroup& grp, const CyclicGroup::Slot& slot, bool isWeekly)
    {
        // Group-level equivalent of the Basic Event apiWorldBossId check
        // above, applying to the WHOLE group rather than just this one
        // slot. No-op for every group except the 8 HoT/PoF maps
        // /v2/account/mapchests covers. Independent of the per-SLOT
        // isWeekly check this function takes in — different reward
        // track, different reset schedule.
        if (!grp.apiMapChestId.empty() && IsMapChestClaimedToday(grp.apiMapChestId))
            return;

        // Manual counterpart, per-slot rather than group-level — see
        //events_tracking.h and the identical check in subscriptions_window.cpp.
        if (IsCyclicSlotMarkedDoneToday({ grp.name, slot.offset }))
            return;

        int secondsOfDay = (int)(now % grp.period);
        int repeat  = slot.repeat > 0 ? slot.repeat : 1;
        int subSpan = grp.period / repeat;

        bool foundActive    = false;
        int  activeSecsLeft = 0;
        int  bestSecsUntil  = grp.period;

        // Find whichever repeat of this slot is active, else the soonest upcoming one.
        for (int r = 0; r < repeat; r++)
        {
            int baseOffset     = slot.offset + r * subSpan;
            int phase          = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
            bool slotActive    = (phase < slot.duration);
            int secsUntilStart = slotActive ? 0 : (grp.period - phase);

            if (slotActive)
            {
                foundActive    = true;
                activeSecsLeft = slot.duration - phase;
                break;
            }
            else if (secsUntilStart < bestSecsUntil)
            {
                bestSecsUntil = secsUntilStart;
            }
        }

        int startSec, endSec, statusSecs;
        bool active;
        if (foundActive)
        {
            active     = true;
            startSec   = 0;
            endSec     = std::min(activeSecsLeft, kWindowSeconds);
            statusSecs = activeSecsLeft;
        }
        else
        {
            if (bestSecsUntil >= kWindowSeconds) return; // not upcoming within the window
            active     = false;
            startSec   = bestSecsUntil;
            endSec     = std::min(bestSecsUntil + slot.duration, kWindowSeconds);
            statusSecs = bestSecsUntil;
        }

        if (endSec <= startSec) return;
        if (active && SubscriptionsBarHideActive) return;

        char offsetBuf[16];
        snprintf(offsetBuf, sizeof(offsetBuf), "%d", slot.offset);
        std::string label = grp.name + " - " + slot.name;
        LineSegment seg{
            "Cyclic:" + grp.name + ":" + offsetBuf, label, slot.chatCode,
            secToX(startSec), secToX(endSec), active, statusSecs, endSec - startSec,
            grp.SlotColor(slot)
        };
        seg.isWeekly  = isWeekly;
        seg.isBasic   = false;
        seg.cyclicKey = CyclicSubscriptionKey{ grp.name, slot.offset };
        segs.push_back(seg);
    };

    for (const auto& key : g_SubscribedCyclicSlots)
    {
        auto it = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
            [&](const CyclicGroup& grp) { return grp.name == key.groupName; });
        if (it == g_CyclicGroups.end()) continue; // group deleted since subscribing

        for (const auto& slot : it->slots)
        {
            if (slot.offset != key.slotOffset) continue;
            bool weeklyComplete = false;
            bool isWeekly = IsCyclicSlotWeeklyTarget(it->name, slot.name, weeklyComplete) && !weeklyComplete;
            AddCyclicSegment(*it, slot, isWeekly);
            break;
        }
    }

    // Auto-tracked weekly targets not already manually subscribed — same
    // rule as the Basic Events pass above, gated by the same
    // WeeklyAutoTrackEnabled master switch.
    if (WeeklyAutoTrackEnabled)
    {
        for (const auto& grp : g_CyclicGroups)
        {
            for (const auto& slot : grp.slots)
            {
                bool alreadyManual = std::find_if(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(),
                    [&](const CyclicSubscriptionKey& k) { return k.groupName == grp.name && k.slotOffset == slot.offset; })
                    != g_SubscribedCyclicSlots.end();
                if (alreadyManual) continue;

                bool weeklyComplete = false;
                if (!IsCyclicSlotWeeklyTarget(grp.name, slot.name, weeklyComplete)) continue;
                if (weeklyComplete) continue;

                AddCyclicSegment(grp, slot, true);
            }
        }
    }

    std::sort(segs.begin(), segs.end(), [](const LineSegment& a, const LineSegment& b)
    {
        return a.startX < b.startX;
    });

    AssignLanes(segs);

    return segs;
}

// Ken Perlin's smoothstep easing.
static float SmoothStep(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Depth profile (0..1) at local-x for a flat-top block with curved
// shoulders of width tw, confined within [start, end]: flat 0 outside the
// range, eases up across [start, start+tw], flat 1 across the middle,
// eases back down across [end-tw, end]. For segments narrower than 2*tw,
// tw is capped to half the segment's width.
static float FlatBlockDepthAt(float x, float start, float end, float tw)
{
    float effectiveTw = std::min(tw, (end - start) * 0.5f);
    if (effectiveTw <= 0.0f) return (x >= start && x <= end) ? 1.0f : 0.0f;

    if (x < start)               return 0.0f;
    if (x < start + effectiveTw) return SmoothStep((x - start) / effectiveTw);
    if (x <= end - effectiveTw)  return 1.0f;
    if (x <= end)                 return SmoothStep((end - x) / effectiveTw);
    return 0.0f;
}

// Per-segment eased drop animation state, keyed by LineSegment::key.
// amount: current eased drop depth (0..1), toward a target driven by hover.
// hoverSeconds: continuous real-hover duration; resets to 0 on losing hover.
// clickHoldSeconds: counts down while a click-triggered pop-out is held
// open; while > 0 the segment is treated as hovered by the easing loop.
struct DropState { float amount = 0.0f; float hoverSeconds = 0.0f; float clickHoldSeconds = 0.0f; };
static std::unordered_map<std::string, DropState> s_dropStates;

// Assigns each currently-hovered/dropping segment a row (0, 1, 2, ...) for
// the vertical stack, so segments whose x-ranges don't overlap can share a
// row. Processes longest-duration-first (durationSecs descending,
// soonest-first as tiebreak), so a longer-running segment wins the lowest
// available row over a shorter one it overlaps. Uses simple greedy
// interval-graph coloring, same approach as AssignLanes.
struct StackRowInfo
{
    int   row = 0;
    float rowMaxDepth = 0.0f; // currently unused by the row-to-Y conversion (see stackTopY)
};
static std::unordered_map<std::string, StackRowInfo> PackStackRows(
    const std::vector<LineSegment>& segs,
    const std::vector<int>& order, // indices into segs, soonest-first
    const std::unordered_map<std::string, DropState>& dropStates,
    const std::unordered_map<std::string, std::pair<float, float>>& dropBoundsByKey) // per-key edge-safe [dropX0, dropX1]; overlap is checked against these, not the raw x-range
{
    constexpr float kRowMargin = 4.0f; // px of extra breathing room between two segments sharing a row
    std::unordered_map<std::string, StackRowInfo> result;

    std::vector<int> durationOrder = order;
    std::stable_sort(durationOrder.begin(), durationOrder.end(),
        [&](int a, int b)
        {
            return segs[a].durationSecs > segs[b].durationSecs;
        });

    std::vector<std::vector<std::pair<float, float>>> rowSpans; // every occupant's [startX, endX] placed in each row so far
    std::vector<float> rowDepth;  // rowDepth[row] = max eased depth among segments placed in that row so far

    for (int idx : durationOrder)
    {
        const LineSegment& s = segs[idx];
        auto dsIt = dropStates.find(s.key);
        float depth = (dsIt != dropStates.end()) ? dsIt->second.amount : 0.0f;

        float sStartX = s.startX, sEndX = s.endX;
        {
            auto boundsIt = dropBoundsByKey.find(s.key);
            if (boundsIt != dropBoundsByKey.end())
            {
                sStartX = boundsIt->second.first;
                sEndX   = boundsIt->second.second;
            }
        }

        int chosenRow = -1;
        for (int row = 0; row < (int)rowSpans.size(); row++)
        {
            bool fits = true;
            for (const auto& span : rowSpans[row])
            {
                if (sStartX < span.second - kRowMargin && sEndX > span.first + kRowMargin)
                {
                    fits = false;
                    break;
                }
            }
            if (fits) { chosenRow = row; break; }
        }
        if (chosenRow < 0)
        {
            chosenRow = (int)rowSpans.size();
            rowSpans.push_back({});
            rowDepth.push_back(0.0f);
        }

        rowSpans[chosenRow].push_back({sStartX, sEndX});
        rowDepth[chosenRow] = std::max(rowDepth[chosenRow], depth);
        result[s.key].row = chosenRow;
    }

    // Second pass: write each row's final max depth back once all its occupants are known.
    for (int idx : order)
    {
        const LineSegment& s = segs[idx];
        auto it = result.find(s.key);
        if (it != result.end()) it->second.rowMaxDepth = rowDepth[it->second.row];
    }

    return result;
}


static constexpr float kTransitionWidth = 26.0f; // px — curved shoulder width
static constexpr float kEaseRate        = 0.18f; // per-frame lerp factor

// Pill-detach phase thresholds (fractions of DropState::amount, 0..1):
//   [0, kPinchStart)          normal grow, attached to baseline
//   [kPinchStart, kPinchEnd)  shoulders neck inward, still attached
//   [kPinchEnd, kDetachEnd)   detached pill, easing width/height/Y toward rest
//   [kDetachEnd, 1]           fully resolved floating pill, label shown
static constexpr float kPinchStart = 0.60f;
static constexpr float kPinchEnd   = 0.78f;
static constexpr float kDetachEnd  = 0.92f;

// Widens a segment's dropped-block x-range to at least minWidth (its own
// label's required width), growing to the right first, then spilling any
// remainder left, clamped so it never crosses the opposite screen edge.
// Only ever widens — an already-wide segment is returned unchanged.
static void EdgeSafeDropBounds(float startX, float endX, float screenW, float minWidth, float& outX0, float& outX1)
{
    float naturalW = endX - startX;
    if (naturalW >= minWidth)
    {
        outX0 = startX;
        outX1 = endX;
        return;
    }

    float deficit = minWidth - naturalW;
    float growRight = std::min(deficit, std::max(0.0f, screenW - endX));
    float remaining = deficit - growRight;
    float growLeft  = std::min(remaining, std::max(0.0f, startX));

    outX0 = startX - growLeft;
    outX1 = endX + growRight;
}

// 0..1 amount of inward "waist" pinch at x, for the neck-in sub-phase
// (neckT 0..1). Reuses FlatBlockDepthAt's shoulder logic centered on the
// segment's middle. Peaks mid-phase and returns to 0 at both ends of the
// sub-phase, so the shape is a plain rectangle exactly at neckT==1, ready
// for the detached-pill branch to take over.
static float PillPinchFactor(float x, float start, float end, float neckT)
{
    if (neckT <= 0.0f || neckT >= 1.0f) return 0.0f;
    float half = (end - start) * 0.5f;
    if (half <= 0.0f) return 0.0f;
    float distFromEdge = std::min(x - start, end - x) / half; // 0 at either edge, 1 at center
    float waistShape = 1.0f - std::min(1.0f, distFromEdge / 0.55f); // strongest away from center
    float envelope = (neckT < 0.6f) ? SmoothStep(neckT / 0.6f) : SmoothStep((1.0f - neckT) / 0.4f);
    return std::max(0.0f, waistShape * envelope);
}

// Builds the flat-top/curved-shoulder silhouette as real geometry: two
// cubic Bezier splines for the rising/falling shoulders (control points
// digitized from the reference SVG's half-Gaussian curve) plus straight
// runs for the flat top and baseline. depth scales how far the flat top
// reaches from baselineY; dropDir flips the direction for bottom-anchored
// mode (+1 grows down, -1 grows up).
static void PathFlatBlockShoulders(ImDrawList* dl, float start, float end, float baselineY, float blockH, float tw, float depth, float dropDir = 1.0f)
{
    float effectiveTw = std::min(tw, (end - start) * 0.5f);
    float h = dropDir * blockH * depth; // signed distance of the flat top from the baseline

    if (effectiveTw <= 0.0f)
    {
        // Degenerate (very narrow segment): plain rect, no shoulders.
        dl->PathLineTo(ImVec2(start, baselineY));
        dl->PathLineTo(ImVec2(start, baselineY + h));
        dl->PathLineTo(ImVec2(end, baselineY + h));
        dl->PathLineTo(ImVec2(end, baselineY));
        return;
    }

    // fx/fy fractions per point: fx is 0 at the baseline corner, 1 at the flat-top
    // corner; fy is 0 at the baseline, 1 at the flat top.
    struct Pt { float fx, fy; };
    static const Pt kRise[] = {
        { 0.000f, 0.000f },                                       // P0: baseline
        { 0.212f, 0.000f }, { 0.259f, 0.040f }, { 0.318f, 0.140f }, // C1
        { 0.376f, 0.250f }, { 0.412f, 0.380f }, { 0.447f, 0.520f }, // C2
        { 0.482f, 0.660f }, { 0.529f, 0.780f }, { 0.612f, 0.860f }, // C3
        { 0.694f, 0.940f }, { 0.824f, 0.980f }, { 1.000f, 1.000f }, // C4: flat top
    };
    constexpr int kNumPts = sizeof(kRise) / sizeof(kRise[0]);

    auto toRisePoint = [&](const Pt& p) {
        return ImVec2(start + effectiveTw * p.fx, baselineY + h * p.fy);
    };
    auto toFallPoint = [&](const Pt& p) {
        // Falling shoulder is the rising one mirrored horizontally, walked start-to-end.
        return ImVec2(end - effectiveTw * p.fx, baselineY + h * p.fy);
    };

    constexpr int kSegsPerCubic = 8; // fixed tessellation so rise/fall curves stay symmetric

    ImVec2 riseP0 = toRisePoint(kRise[0]);
    dl->PathLineTo(riseP0);
    for (int i = 1; i < kNumPts; i += 3)
    {
        ImVec2 cp1 = toRisePoint(kRise[i]);
        ImVec2 cp2 = toRisePoint(kRise[i + 1]);
        ImVec2 ep  = toRisePoint(kRise[i + 2]);
        dl->PathBezierCubicCurveTo(cp1, cp2, ep, kSegsPerCubic);
    }

    // Flat block top, drawn explicitly to the fall spline's own start point.
    ImVec2 flatEnd = toFallPoint(kRise[kNumPts - 1]);
    dl->PathLineTo(flatEnd);

    // Falling shoulder: same spline walked back-to-front, mirrored via toFallPoint.
    for (int i = kNumPts - 4; i >= 0; i -= 3)
    {
        ImVec2 cp1 = toFallPoint(kRise[i + 2]);
        ImVec2 cp2 = toFallPoint(kRise[i + 1]);
        ImVec2 ep  = toFallPoint(kRise[i]);
        dl->PathBezierCubicCurveTo(cp1, cp2, ep, kSegsPerCubic);
    }
}

// Fills the exact silhouette PathFlatBlockShoulders() strokes, as a single
// triangulated mesh (center rect + two shoulder-cap fans, sharing vertex
// indices at both seams) so there is no crack between separately-drawn
// pieces. Tessellates the rise/fall curves via the same calls
// PathFlatBlockShoulders uses, so the fill matches the stroke point-for-point.
static void FillFlatBlockShoulders(ImDrawList* dl, float start, float end, float baselineY, float blockH, float tw, float depth, ImU32 fillColor, float dropDir = 1.0f)
{
    float effectiveTw = std::min(tw, (end - start) * 0.5f);
    float h = dropDir * blockH * depth;

    if (effectiveTw <= 0.0f)
    {
        dl->AddRectFilled(ImVec2(start, baselineY), ImVec2(end, baselineY + h), fillColor);
        return;
    }

    struct Pt { float fx, fy; };
    static const Pt kRise[] = {
        { 0.000f, 0.000f },
        { 0.212f, 0.000f }, { 0.259f, 0.040f }, { 0.318f, 0.140f },
        { 0.376f, 0.250f }, { 0.412f, 0.380f }, { 0.447f, 0.520f },
        { 0.482f, 0.660f }, { 0.529f, 0.780f }, { 0.612f, 0.860f },
        { 0.694f, 0.940f }, { 0.824f, 0.980f }, { 1.000f, 1.000f },
    };
    constexpr int kNumPts = sizeof(kRise) / sizeof(kRise[0]);
    constexpr int kSegsPerCubic = 8; // must match PathFlatBlockShoulders

    auto toRisePoint = [&](const Pt& p) { return ImVec2(start + effectiveTw * p.fx, baselineY + h * p.fy); };
    auto toFallPoint = [&](const Pt& p) { return ImVec2(end - effectiveTw * p.fx, baselineY + h * p.fy); };

    dl->PathClear();
    dl->PathLineTo(toRisePoint(kRise[0]));
    for (int i = 1; i < kNumPts; i += 3)
        dl->PathBezierCubicCurveTo(toRisePoint(kRise[i]), toRisePoint(kRise[i + 1]), toRisePoint(kRise[i + 2]), kSegsPerCubic);
    std::vector<ImVec2> risePts(dl->_Path.Data, dl->_Path.Data + dl->_Path.Size);
    dl->PathClear();

    dl->PathLineTo(toFallPoint(kRise[0]));
    for (int i = 1; i < kNumPts; i += 3)
        dl->PathBezierCubicCurveTo(toFallPoint(kRise[i]), toFallPoint(kRise[i + 1]), toFallPoint(kRise[i + 2]), kSegsPerCubic);
    std::vector<ImVec2> fallPts(dl->_Path.Data, dl->_Path.Data + dl->_Path.Size);
    dl->PathClear();

    int riseN = (int)risePts.size();
    int fallN = (int)fallPts.size();
    ImVec2 innerTopLeft (start + effectiveTw, baselineY);
    ImVec2 innerTopRight(end   - effectiveTw, baselineY);

    // One combined vertex buffer (rise points, fall points, two inner-top
    // corners); every triangle below indexes into it, including at the
    // rect/cap seams, so shared edges use the same vertex, not a copy.
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    int vtxCount = riseN + fallN + 2;
    int triCount = (riseN - 1) + (fallN - 1) + 2; // left fan + right fan + rect (2 tris)
    dl->PrimReserve(triCount * 3, vtxCount);

    unsigned int base = dl->_VtxCurrentIdx;
    for (int i = 0; i < riseN; i++) { dl->_VtxWritePtr->pos = risePts[i]; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++; }
    for (int i = 0; i < fallN; i++) { dl->_VtxWritePtr->pos = fallPts[i]; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++; }
    dl->_VtxWritePtr->pos = innerTopLeft;  dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++;
    dl->_VtxWritePtr->pos = innerTopRight; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++;
    dl->_VtxCurrentIdx += (unsigned int)vtxCount;

    unsigned int idxRise0        = base;
    unsigned int idxRiseInner    = base + (riseN - 1); // L_bottom
    unsigned int idxFall0        = base + riseN;
    unsigned int idxFallInner    = base + riseN + (fallN - 1); // R_bottom
    unsigned int idxInnerTopLeft  = base + riseN + fallN;      // L_top
    unsigned int idxInnerTopRight = base + riseN + fallN + 1;  // R_top

    auto tri = [&](unsigned int a, unsigned int b, unsigned int c)
    {
        dl->_IdxWritePtr[0] = (ImDrawIdx)a; dl->_IdxWritePtr[1] = (ImDrawIdx)b; dl->_IdxWritePtr[2] = (ImDrawIdx)c;
        dl->_IdxWritePtr += 3;
    };

    // Left cap: fan from its inner-top corner across the rise curve's points.
    for (int i = 0; i < riseN - 1; i++)
        tri(idxInnerTopLeft, idxRise0 + i, idxRise0 + i + 1);

    // Right cap: mirror, fan from its own inner-top corner.
    for (int i = 0; i < fallN - 1; i++)
        tri(idxInnerTopRight, idxFall0 + i, idxFall0 + i + 1);

    // Center rectangle, as 2 more triangles in the same mesh as the caps.
    tri(idxInnerTopLeft, idxInnerTopRight, idxFallInner);
    tri(idxInnerTopLeft, idxFallInner, idxRiseInner);
}

// Rounded-rect path via direct PathArcTo calls, with a radius-scaled
// segment count, so a full stadium cap (rx = height/2) renders smoothly
// instead of AddRect's fixed 3-segments-per-corner faceting. Same
// winding/geometry as AddRect (left side bottom-left->top-left arcs, right
// side top-right->bottom-right arcs), so it's a drop-in replacement.
static void PathRoundedRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding)
{
    constexpr float kPi = 3.14159265358979323846f;
    rounding = std::max(0.0f, std::min(rounding, std::min((p1.x - p0.x) * 0.5f, (p1.y - p0.y) * 0.5f)));

    if (rounding <= 0.0f)
    {
        dl->PathLineTo(ImVec2(p0.x, p0.y));
        dl->PathLineTo(ImVec2(p1.x, p0.y));
        dl->PathLineTo(ImVec2(p1.x, p1.y));
        dl->PathLineTo(ImVec2(p0.x, p1.y));
        return;
    }

    int segsPerQuarter = std::max(4, std::min(16, (int)(rounding * 0.5f)));

    float x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    dl->PathArcTo(ImVec2(x0 + rounding, y1 - rounding), rounding, kPi * 0.5f, kPi,        segsPerQuarter); // bottom-left
    dl->PathArcTo(ImVec2(x0 + rounding, y0 + rounding), rounding, kPi,        kPi * 1.5f, segsPerQuarter); // top-left
    dl->PathArcTo(ImVec2(x1 - rounding, y0 + rounding), rounding, kPi * 1.5f, kPi * 2.0f, segsPerQuarter); // top-right
    dl->PathArcTo(ImVec2(x1 - rounding, y1 - rounding), rounding, 0.0f,       kPi * 0.5f, segsPerQuarter); // bottom-right
}

void RenderSubscriptionsBar()
{
    if (!ShowSubscriptionsBar) return;
    if (g_SubscribedBasicEvents.empty() && g_SubscribedCyclicSlots.empty())
    {
        s_dropStates.clear();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y; // only used by SubscriptionsBarBottomAnchored
    if (screenW <= 0.0f) return;

    time_t now = time(nullptr);
    std::vector<LineSegment> segs = CollectVisibleSegments(now, screenW);
    if (segs.empty())
    {
        s_dropStates.clear();
        return;
    }
    // Minimal mode hides the per-segment colored baseline, so every event
    // needs its own dot rather than just the hidden lane>0 ones.
    std::vector<DotMark> dots = SubscriptionsBarMinimalMode
        ? CollectAllEventDots(segs)
        : CollectOverlapDots(segs, screenW);

    // Layout constants (local space: y=0 is the baseline strip). kDropDir
    // flips every depth-based offset so blocks/pills/dots grow up off the
    // bottom edge when bottom-anchored instead of down off the top edge.
    constexpr float kLineThick    = 2.0f;
    const float kDropDir = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
    const float kBaselineY = SubscriptionsBarBottomAnchored
        ? (screenH - kLineThick * 0.5f)
        : (kLineThick * 0.5f);
    const float kMaxDropPx = (float)std::max(8, SubscriptionsBarMaxDropPx); // floored so pill radius math stays positive
    constexpr float kGapPx        = 3.0f;  // notch between adjacent lane-0 segments
    constexpr float kStackGapPx   = 4.0f;  // vertical gap between stacked dropped blocks
    constexpr float kDotRadius    = 2.5f;  // px
    constexpr float kDotSpacingPx = 7.0f;  // horizontal spacing between dots sharing a tick
    // Normal mode offsets dots below the colored line; minimal mode has no
    // colored line, so its dots sit directly on the baseline instead.
    const float kDotY = SubscriptionsBarMinimalMode ? kBaselineY : (kBaselineY + kDropDir * 8.0f);
    constexpr float kDotHitRadius = 5.0f;  // click/hover target radius around each dot
    constexpr float kLabelPadX    = 6.0f;  // label plate padding
    constexpr float kLabelPadY    = 3.0f;

    // Nudge co-occurring dots (same tick) apart horizontally so they render
    // as a small cluster of distinct dots rather than one blob.
    std::vector<float> dotDrawX(dots.size());
    {
        size_t i = 0;
        while (i < dots.size())
        {
            size_t j = i;
            while (j < dots.size() && fabsf(dots[j].x - dots[i].x) < 0.5f) j++; // group exact-tie ticks
            size_t groupCount = j - i;
            float groupCenter = dots[i].x;
            float groupStart  = groupCenter - (groupCount - 1) * kDotSpacingPx * 0.5f;
            for (size_t k = i; k < j; k++) dotDrawX[k] = groupStart + (k - i) * kDotSpacingPx;
            i = j;
        }
    }

    ImVec2 mouse = io.MousePos;
    bool mouseValid = io.MousePos.x > -FLT_MAX; // ImGui reports (-FLT_MAX,-FLT_MAX) when there's no mouse

    // Which segments are currently under the mouse, via two paths:
    //  1. Mouse over a lane-0 segment's own x-range, within a shared
    //     vertical band from the baseline down to whatever's deepest
    //     currently open (so the mouse can travel from the line down onto
    //     an already-open block without losing hover mid-transit).
    //  2. Mouse over one of that segment's dot markers (lane>0 segments
    //     have no line of their own).
    // Both feed the same hoveredIndices list.
    constexpr float kLineHalfHeight = (kLineThick + 1.0f) * 0.5f;
    constexpr float kHoverBand = kLineHalfHeight + 2.0f; // the only band that can start a fresh pop-out

    // Shared sustain-band far edge: baseline by default, pushed out along
    // kDropDir to cover whichever currently-mid-drop segment(s) reach
    // deepest, estimated conservatively from last frame's state (one row
    // per open segment, an upper bound on the real packed stack height).
    float sustainBottom = kBaselineY + kDropDir * kHoverBand;
    {
        std::vector<int> openLastFrame;
        for (int i = 0; i < (int)segs.size(); i++)
            if (s_dropStates[segs[i].key].amount > 0.001f) openLastFrame.push_back(i);
        std::sort(openLastFrame.begin(), openLastFrame.end(), [&](int a, int b)
        {
            return segs[a].statusSecs < segs[b].statusSecs;
        });

        float runningY = kBaselineY;
        float unsafeBase = (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
        float runningPillY = kBaselineY + kDropDir * unsafeBase;
        for (int idx : openLastFrame)
        {
            const LineSegment& s = segs[idx];
            float depth = s_dropStates[s.key].amount;
            float topY = runningY;

            bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
            float detachT = inUnsafeZone ? std::min(1.0f, std::max(0.0f, (depth - kPinchEnd) / (kDetachEnd - kPinchEnd))) : 0.0f;
            float pillY = topY;
            if (detachT > 0.0f)
            {
                pillY = topY + (runningPillY - topY) * detachT;
                runningPillY += kDropDir * (kMaxDropPx + kStackGapPx);
            }
            float blockFar = (kDropDir > 0.0f ? std::max(topY, pillY) : std::min(topY, pillY)) + kDropDir * kMaxDropPx;

            sustainBottom = (kDropDir > 0.0f)
                ? std::max(sustainBottom, blockFar + 4.0f)
                : std::min(sustainBottom, blockFar - 4.0f);

            runningY += kDropDir * (kMaxDropPx * depth + kStackGapPx * depth);
        }
    }
    std::vector<int> hoveredIndices;
    if (mouseValid)
    {
        if (mouse.y >= kBaselineY - kHoverBand && mouse.y <= kBaselineY + kHoverBand)
        {
            for (int i = 0; i < (int)segs.size(); i++)
            {
                if (segs[i].lane != 0) continue; // lane>0 segments have no line of their own to hover
                if (mouse.x >= segs[i].startX && mouse.x < segs[i].endX) hoveredIndices.push_back(i);
            }
        }
        // Sustain path: segments already mid-drop, tested against the
        // shared band (baseline to sustainBottom) over their own x-range
        // padded by kSustainPadX, so a dot-triggered open doesn't collapse
        // the moment the mouse leaves the dot's tiny hit-circle.
        float sustainNear = kBaselineY - kDropDir * kHoverBand;
        float sustainLo = std::min(sustainNear, sustainBottom);
        float sustainHi = std::max(sustainNear, sustainBottom);
        if (mouse.y >= sustainLo && mouse.y <= sustainHi)
        {
            constexpr float kSustainPadX = 8.0f; // px of forgiveness left/right of the opened element's own width
            for (int i = 0; i < (int)segs.size(); i++)
            {
                if (s_dropStates[segs[i].key].amount <= 0.0f) continue;
                if (mouse.x < segs[i].startX - kSustainPadX || mouse.x >= segs[i].endX + kSustainPadX) continue;
                if (std::find(hoveredIndices.begin(), hoveredIndices.end(), i) == hoveredIndices.end())
                    hoveredIndices.push_back(i);
            }
        }
        for (size_t d = 0; d < dots.size(); d++)
        {
            float dx = mouse.x - dotDrawX[d];
            float dy = mouse.y - kDotY;
            if (dx * dx + dy * dy <= kDotHitRadius * kDotHitRadius)
            {
                int idx = dots[d].segIndex;
                if (std::find(hoveredIndices.begin(), hoveredIndices.end(), idx) == hoveredIndices.end())
                    hoveredIndices.push_back(idx);
            }
        }
    }
    // Fold in any segment held open by a click (see DropState::clickHoldSeconds).
    for (int i = 0; i < (int)segs.size(); i++)
    {
        if (s_dropStates[segs[i].key].clickHoldSeconds <= 0.0f) continue;
        if (std::find(hoveredIndices.begin(), hoveredIndices.end(), i) == hoveredIndices.end())
            hoveredIndices.push_back(i);
    }
    // Stacking order: soonest-starting (or active) segment on top. Must be
    // a stable sort (with key as a final tiebreak) so exact statusSecs
    // ties don't reorder from frame to frame, which would also destabilize
    // PackStackRows' own tiebreak.
    std::stable_sort(hoveredIndices.begin(), hoveredIndices.end(), [&](int a, int b)
    {
        if (segs[a].statusSecs != segs[b].statusSecs) return segs[a].statusSecs < segs[b].statusSecs;
        return segs[a].key < segs[b].key;
    });

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ---- Track raw hover duration, then ease every segment's drop amount toward a delay-gated target ----
    float dt = io.DeltaTime > 0.0f ? std::min(io.DeltaTime, 0.1f) : 0.0f;
    // Frame-rate-independent lerp: rate raised to a power of (dt*60) keeps the same half-life at any frame rate.
    float easeThisFrame = 1.0f - powf(1.0f - kEaseRate, dt > 0.0f ? dt * 60.0f : 1.0f);
    float hoverDelaySeconds = std::max(0, SubscriptionsBarHoverDelayMs) / 1000.0f;

    for (int i = 0; i < (int)segs.size(); i++)
    {
        DropState& st = s_dropStates[segs[i].key];

        // clickHoldSeconds counts down once per frame and makes this
        // segment count as hovered regardless of real mouse position.
        // forcePastDelay only applies when the click that set it had
        // already cleared this segment's own hover delay through real
        // dwell time (enforced at the click site further down).
        bool isHovered = std::find(hoveredIndices.begin(), hoveredIndices.end(), i) != hoveredIndices.end();
        bool forcePastDelay = false;
        if (st.clickHoldSeconds > 0.0f)
        {
            isHovered = true;
            forcePastDelay = true;
            st.clickHoldSeconds = std::max(0.0f, st.clickHoldSeconds - dt);
        }

        // Raw hover duration resets instantly on losing hover.
        st.hoverSeconds = isHovered ? (st.hoverSeconds + dt) : 0.0f;

        // Drop targets 1.0 only once raw hover has cleared the configured
        // delay; losing hover always targets 0 immediately (no delay on the way back down).
        bool pastDelay = isHovered && (forcePastDelay || st.hoverSeconds >= hoverDelaySeconds);
        float target = pastDelay ? 1.0f : 0.0f;

        st.amount += (target - st.amount) * easeThisFrame;
        if (fabsf(st.amount - target) < 0.001f) st.amount = target;
    }

    // Drop stale keys (segment no longer visible this frame) so the map doesn't grow unbounded.
    if (s_dropStates.size() > segs.size() * 2 + 8)
    {
        std::vector<std::string> liveKeys;
        liveKeys.reserve(segs.size());
        for (auto& s : segs) liveKeys.push_back(s.key);
        for (auto it2 = s_dropStates.begin(); it2 != s_dropStates.end(); )
        {
            if (std::find(liveKeys.begin(), liveKeys.end(), it2->first) == liveKeys.end() && it2->second.amount < 0.001f)
                it2 = s_dropStates.erase(it2);
            else
                ++it2;
        }
    }

    // ---- Baseline: thin ambient rail across the full screen width (drawn in both modes) ----
    dl->AddLine(ImVec2(0, kBaselineY), ImVec2(screenW, kBaselineY),
        IM_COL32(255, 255, 255, 90), kLineThick);

    // ---- Dot markers: white, red for an active-and-incomplete weekly
    // Wizard's Vault target (see weekly_vault.h/.cpp for what sets
    // isWeekly and why) — fading out as their own segment drops in ----
    for (size_t d = 0; d < dots.size(); d++)
    {
        const LineSegment& dotSeg = segs[dots[d].segIndex];
        float depth = s_dropStates[dotSeg.key].amount;
        float alpha = 1.0f - depth;
        if (alpha <= 0.02f) continue;
        ImU32 dotColor = dotSeg.isWeekly
            ? IM_COL32(220, 40, 40, (int)(235 * alpha))
            : IM_COL32(255, 255, 255, (int)(235 * alpha));
        dl->AddCircleFilled(ImVec2(dotDrawX[d], kDotY), kDotRadius, dotColor, 12);
    }

    // Top-of-block Y for each hovered segment, filled in from the row
    // packing below; drops always grow from the baseline itself, and only
    // move toward the unsafe-zone clearance once fully detached into a pill.
    std::unordered_map<std::string, float> stackTopY;

    // ---- Edge-safe drop bounds, precomputed once per segment so row-packing, drawing, and click hit-testing all agree ----
    std::unordered_map<std::string, std::pair<float, float>> dropBoundsByKey;
    dropBoundsByKey.reserve(segs.size());
    for (const auto& seg : segs)
    {
        float bx0 = seg.startX;
        float bx1 = seg.endX;
        if (seg.endX < screenW) bx1 -= kGapPx;
        if (bx1 <= bx0) bx1 = bx0 + 1.0f;

        // Minimum drop width derived from this segment's own label text + plate padding.
        ImVec2 nameSize   = ImGui::CalcTextSize(seg.name.c_str());
        ImVec2 statusSize = ImGui::CalcTextSize(SegmentStatusLine(seg).c_str());
        float minWidth = std::max(nameSize.x, statusSize.x) + kLabelPadX * 2.0f;

        float dropX0, dropX1;
        EdgeSafeDropBounds(bx0, bx1, screenW, minWidth, dropX0, dropX1);
        dropBoundsByKey[seg.key] = { dropX0, dropX1 };
    }

    std::unordered_map<std::string, StackRowInfo> stackRows = PackStackRows(segs, hoveredIndices, s_dropStates, dropBoundsByKey);

    {
        // Convert each row index into a cumulative Y. Row height is NOT
        // scaled by live eased depth (a fully-open row and a still-easing
        // row below it must never overlap), so every occupied row
        // reserves full kMaxDropPx unconditionally.
        int maxRow = -1;
        for (auto& kv : stackRows) maxRow = std::max(maxRow, kv.second.row);

        std::vector<float> rowTopY(maxRow + 1, 0.0f);
        float runningY = kBaselineY;
        for (int row = 0; row <= maxRow; row++)
        {
            rowTopY[row] = runningY;
            runningY += kDropDir * (kMaxDropPx + kStackGapPx);
        }

        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto it = stackRows.find(s.key);
            if (it != stackRows.end()) stackTopY[s.key] = rowTopY[it->second.row];
        }
    }

    // Per-segment resting Y for detached pills, so multiple simultaneously-
    // open pills stagger below the configured clearance instead of
    // converging on the same absolute Y.
    std::unordered_map<std::string, float> pillStackY;
    {
        // Base clearance is the larger of the configured unsafe-zone
        // clearance and "past every attached row above the first pill
        // row" (so a pill can't rest inside an ordinary attached block
        // occupying the same space).
        int lowestPillRow = INT_MAX;
        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto rowIt = stackRows.find(s.key);
            if (rowIt == stackRows.end()) continue;
            bool stackDetach = rowIt->second.row > 0;
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
            if (inUnsafeZone || stackDetach) lowestPillRow = std::min(lowestPillRow, rowIt->second.row);
        }

        // "Farther along kDropDir": larger y when top-anchored, smaller y when bottom-anchored.
        auto farther = [&](float a, float b) { return (kDropDir > 0.0f) ? std::max(a, b) : std::min(a, b); };

        float unsafeRestY = kBaselineY + kDropDir * (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
        float runningPillY = unsafeRestY;
        if (lowestPillRow != INT_MAX)
        {
            for (int row = 0; row < lowestPillRow; row++)
            {
                auto rowTopIt = std::find_if(stackTopY.begin(), stackTopY.end(),
                    [&](const std::pair<const std::string, float>& kv)
                    {
                        auto ri = stackRows.find(kv.first);
                        return ri != stackRows.end() && ri->second.row == row;
                    });
                if (rowTopIt != stackTopY.end())
                    runningPillY = farther(runningPillY, rowTopIt->second + kDropDir * (kMaxDropPx + kStackGapPx));
            }
        }

        // Keyed by row (not by segment): segments already packed into the
        // same row don't overlap in x, so they share one pill Y slot.
        struct PillCandidate { int row; std::string key; };
        std::vector<PillCandidate> candidates;
        int nextSyntheticRow = -1000000; // negative space, clear of any real row index

        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto rowIt = stackRows.find(s.key);
            bool stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
            if (!inUnsafeZone && !stackDetach) continue; // only pills need a slot here

            float depth = s_dropStates[s.key].amount;
            float detachT = std::min(1.0f, std::max(0.0f, (depth - kPinchEnd) / (kDetachEnd - kPinchEnd)));
            if (detachT <= 0.0f) continue; // not detaching yet

            int row = (rowIt != stackRows.end()) ? rowIt->second.row : nextSyntheticRow--;
            candidates.push_back({row, s.key});
        }

        // Sort by row so slots are handed out row 0, then row 1, etc., regardless of hover-order.
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const PillCandidate& a, const PillCandidate& b) { return a.row < b.row; });

        std::unordered_map<int, float> rowPillY;
        for (const PillCandidate& c : candidates)
        {
            auto slotIt = rowPillY.find(c.row);
            if (slotIt == rowPillY.end())
            {
                rowPillY[c.row] = runningPillY;
                pillStackY[c.key] = runningPillY;
                runningPillY += kDropDir * (kMaxDropPx + kStackGapPx);
            }
            else
            {
                pillStackY[c.key] = slotIt->second;
            }
        }
    }

    // ---- Per-segment colored baseline overlay (lane-0 only) + dropped block/pill ----
    for (int i = 0; i < (int)segs.size(); i++)
    {
        const LineSegment& seg = segs[i];
        float depth = s_dropStates[seg.key].amount;

        // The resting baseline line always uses the segment's true x-range;
        // only the dropped block/pill below uses the edge-safe widened bounds.
        float x0 = seg.startX;
        float x1 = seg.endX;
        float segEnd = x1;
        if (seg.endX < screenW) segEnd -= kGapPx; // small gap so adjacent segments read as distinct blocks
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        ImU32 segColor = seg.color;
        ImU32 fillColor = (segColor & 0x00FFFFFF) | ((ImU32)(255 * (0.25f + 0.75f * (seg.active ? 1.0f : 0.7f))) << 24);

        if (seg.lane == 0 && !SubscriptionsBarMinimalMode)
        {
            // Colored resting line — only lane-0 segments get one; overlap is signaled by dots instead.
            dl->AddLine(ImVec2(x0, kBaselineY), ImVec2(segEnd, kBaselineY), segColor, kLineThick + 1.0f);
        }

        if (depth > 0.002f)
        {
            // Stacked target Y: this segment's reserved slot if hovered,
            // otherwise the baseline (mid-ease-out, no resting Y of its own).
            float topY = kBaselineY;
            auto stackIt = stackTopY.find(seg.key);
            if (stackIt != stackTopY.end()) topY = stackIt->second;

            // Edge-safe drop bounds (only affect the drop shape, not the baseline line above).
            float dropX0 = x0, dropX1 = segEnd;
            {
                auto boundsIt = dropBoundsByKey.find(seg.key);
                if (boundsIt != dropBoundsByKey.end())
                {
                    dropX0 = boundsIt->second.first;
                    dropX1 = boundsIt->second.second;
                }
            }

            float cx = (dropX0 + dropX1) * 0.5f;
            float segW = dropX1 - dropX0;

            // A segment detaches into a pill if it overlaps an unsafe zone, or if it's not in row 0 of the current stack.
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(seg, screenW);
            bool stackDetach = false;
            {
                auto rowIt = stackRows.find(seg.key);
                stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
            }
            bool shouldDetach = inUnsafeZone || stackDetach;

            // Phase split (see kPinchStart/kPinchEnd/kDetachEnd): grow -> neck-in -> detach into a locked-width pill -> settle.
            float pinchT  = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchStart) / (kPinchEnd - kPinchStart))) : 0.0f;
            float detachT = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchEnd)   / (kDetachEnd - kPinchEnd))) : 0.0f;

            // Corner radius eases from 0 (attached) to a stadium cap (rx = h/2) as detachT completes; height stays fixed.
            float blockH = kMaxDropPx;
            float pillRx = (blockH * 0.5f) * detachT;

            // Pill Y eases from topY down to this segment's reserved pillStackY slot as it detaches.
            float unsafeRestY = kBaselineY + kDropDir * (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
            auto pillIt = pillStackY.find(seg.key);
            if (pillIt != pillStackY.end()) unsafeRestY = pillIt->second;
            float pillY = topY + (unsafeRestY - topY) * detachT;

            if (depth < kPinchEnd || !shouldDetach)
            {
                // ---- Phases 1-2 (or the only phase for non-detaching segments): attached FlatBlockDepthAt silhouette, shoulders necking in via PillPinchFactor once past kPinchStart ----
                float tw = kTransitionWidth;

                if (pinchT <= 0.0f)
                {
                    // Common case: real bezier-curve shoulders via PathFlatBlockShoulders/FillFlatBlockShoulders.
                    FillFlatBlockShoulders(dl, dropX0, dropX1, topY, blockH, tw, depth, fillColor, kDropDir);

                    dl->PathClear();
                    PathFlatBlockShoulders(dl, dropX0, dropX1, topY, blockH, tw, depth, kDropDir);
                    dl->PathStroke(segColor, false, kLineThick);
                }
                else
                {
                    // Neck-in sub-phase: PillPinchFactor's waist reshapes every frame, so this samples FlatBlockDepthAt point-by-point instead of using the fixed bezier path.
                    int samples = std::max(8, (int)(segW / 4.0f));

                    dl->PathClear();
                    dl->PathLineTo(ImVec2(dropX0, topY));
                    for (int s = 0; s <= samples; s++)
                    {
                        float x = dropX0 + segW * (s / (float)samples);
                        float d = FlatBlockDepthAt(x, dropX0, dropX1, tw) * depth;
                        float pinch = PillPinchFactor(x, dropX0, dropX1, pinchT);
                        d *= (1.0f - pinch);
                        float y = topY + kDropDir * d * blockH;
                        dl->PathLineTo(ImVec2(x, y));
                    }
                    dl->PathLineTo(ImVec2(dropX1, topY));
                    dl->PathFillConvex(fillColor);

                    dl->PathClear();
                    for (int s = 0; s <= samples; s++)
                    {
                        float x = dropX0 + segW * (s / (float)samples);
                        float d = FlatBlockDepthAt(x, dropX0, dropX1, tw) * depth;
                        float pinch = PillPinchFactor(x, dropX0, dropX1, pinchT);
                        d *= (1.0f - pinch);
                        float y = topY + kDropDir * d * blockH;
                        dl->PathLineTo(ImVec2(x, y));
                    }
                    dl->PathStroke(segColor, false, kLineThick);
                }
            }
            else
            {
                // ---- Phases 3-4: detached. A rounded rect locked to the segment's own on-bar width, easing corner radius up to a full stadium cap ----
                // PathRoundedRect assumes p0.y < p1.y; order by min/max since pillY can be either edge depending on anchor direction.
                float pillOtherY = pillY + kDropDir * blockH;
                ImVec2 pillP0(dropX0, std::min(pillY, pillOtherY));
                ImVec2 pillP1(dropX1, std::max(pillY, pillOtherY));

                dl->PathClear();
                PathRoundedRect(dl, pillP0, pillP1, pillRx);
                dl->PathFillConvex(fillColor);

                dl->PathClear();
                PathRoundedRect(dl, pillP0, pillP1, pillRx);
                dl->PathStroke(segColor, true, kLineThick);
            }

            // Label + status, vertically centered inside the block/pill's own slice of the stack, fading in with depth.
            if (depth > 0.35f)
            {
                std::string line1 = seg.name;
                std::string line2 = SegmentStatusLine(seg);
                ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
                ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());

                // blockNear is whichever edge sits at the pop-out origin (topY or pillY); blockFar is the opposite edge.
                float blockNear   = (depth < kPinchEnd || !shouldDetach) ? topY : pillY;
                float blockFar    = blockNear + kDropDir * blockH;
                float blockTop    = std::min(blockNear, blockFar);
                float blockBottom = std::max(blockNear, blockFar);
                float textBlockH  = size1.y + size2.y;
                float labelY      = blockTop + (blockBottom - blockTop - textBlockH) * 0.5f;

                float alpha = (depth - 0.35f) / 0.65f;
                ImU32 textCol = IM_COL32(255, 255, 255, (int)(230 * alpha));

                // Dark backing plate behind the label so it reads against any game background.
                float plateW = std::max(size1.x, size2.x) + kLabelPadX * 2.0f;
                ImVec2 plateMin(cx - plateW * 0.5f, labelY - kLabelPadY);
                ImVec2 plateMax(cx + plateW * 0.5f, labelY + textBlockH + kLabelPadY);
                ImU32 plateCol = IM_COL32(30, 30, 30, (int)(150 * alpha));
                dl->AddRectFilled(plateMin, plateMax, plateCol, 4.0f);

                dl->AddText(ImVec2(cx - size1.x * 0.5f, labelY), textCol, line1.c_str());
                dl->AddText(ImVec2(cx - size2.x * 0.5f, labelY + size1.y), textCol, line2.c_str());
            }
        }
    }

    // Weekly Wizard's Vault status is shown by recoloring the dot markers
    // themselves red (see the "Dot markers" pass above), not a separate
    // marker pass here.
    // Click on a hovered/dropped segment copies its waypoint code. Only
    // segments that have (nearly) finished dropping are eligible.
    //
    // This bar draws entirely on the background draw list with no
    // ImGui::Begin() window, so a plain IsMouseClicked() check doesn't
    // reliably fire — a small invisible window + InvisibleButton,
    // positioned to match the clickable shape and tested with
    // IsItemClicked(), is used instead (same pattern RenderMapEvents/
    // RenderCyclicGroups use for their own drag anchors).

    // ---- Click-on-the-thin-line: pop out every directly-overlapping segment at once ----
    {
        // One invisible window spans the baseline band across the full
        // screen width; on click, whichever segment the mouse.x falls
        // within (if any) gets force-popped along with everything it
        // directly x-overlaps.
        constexpr float kBottomEdgeSlackPx = 4.0f; // small padding on the bottom-anchored window to reliably catch the bottom-most row
        float lineWinY0 = std::max(SubscriptionsBarBottomAnchored ? -FLT_MAX : 0.0f, kBaselineY - kHoverBand);
        float lineWinY1 = SubscriptionsBarBottomAnchored
            ? (screenH + kBottomEdgeSlackPx)
            : std::min(FLT_MAX, kBaselineY + kHoverBand);

        ImGui::SetNextWindowPos(ImVec2(0, lineWinY0));
        ImGui::SetNextWindowSize(ImVec2(screenW, lineWinY1 - lineWinY0));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // default WindowPadding would offset the clickable rect from the actual line
        // NoBringToFrontOnFocus deliberately omitted: letting a click bring this window to front keeps its z-order self-healing against other ImGui windows.
        ImGui::Begin("##we_subbar_line_click", nullptr,
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoScrollbar     |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        ImGui::InvisibleButton("##we_subbar_line_click_hit", ImVec2(screenW, lineWinY1 - lineWinY0));
        bool lineClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImGui::End();

        if (lineClicked)
        {
            if (mouse.y >= kBaselineY - kHoverBand && mouse.y <= kBaselineY + kHoverBand)
            {
                int clickedIdx = -1;
                for (int i = 0; i < (int)segs.size(); i++)
                {
                    if (segs[i].lane != 0) continue;
                    if (mouse.x >= segs[i].startX && mouse.x < segs[i].endX) { clickedIdx = i; break; }
                }
                // Require the clicked segment to have already cleared the
                // hover delay through real dwell time before a click does anything.
                bool clickedPastDelay = clickedIdx >= 0 &&
                    s_dropStates[segs[clickedIdx].key].hoverSeconds >= hoverDelaySeconds;
                if (clickedPastDelay)
                {
                    constexpr float kClickHoldSeconds = 2.0f; // how long a click-triggered pop-out holds before easing back down on its own
                    // Trigger every segment whose own x-range contains mouse.x (not just the whole clicked span), lane>0 included.
                    for (int j = 0; j < (int)segs.size(); j++)
                    {
                        if (mouse.x >= segs[j].startX && mouse.x < segs[j].endX)
                            s_dropStates[segs[j].key].clickHoldSeconds = kClickHoldSeconds;
                    }
                    io.WantCaptureMouse = true;
                }
            }
        }
    }

    // ---- Click hit-testing (existing): click an already-dropped block or pill to copy its waypoint ----
    for (int idx : hoveredIndices)
    {
        const LineSegment& s = segs[idx];
        float depth = s_dropStates[s.key].amount;
        if (depth <= 0.5f) continue; // not visibly dropped yet

        float x0 = s.startX;
        float segEnd = s.endX;
        if (s.endX < screenW) segEnd -= kGapPx;
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        // Edge-safe bounds from dropBoundsByKey, so the click window matches what was actually drawn/row-packed.
        float dropX0 = x0, dropX1 = segEnd;
        {
            auto boundsIt = dropBoundsByKey.find(s.key);
            if (boundsIt != dropBoundsByKey.end())
            {
                dropX0 = boundsIt->second.first;
                dropX1 = boundsIt->second.second;
            }
        }

        bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
        bool stackDetach = false;
        {
            auto rowIt = stackRows.find(s.key);
            stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
        }
        bool shouldDetach = inUnsafeZone || stackDetach;
        float detachT = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchEnd) / (kDetachEnd - kPinchEnd))) : 0.0f;
        float baseTopY = stackTopY[s.key];
        float unsafeRestY = kBaselineY + kDropDir * (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
        auto pillIt = pillStackY.find(s.key);
        if (pillIt != pillStackY.end()) unsafeRestY = pillIt->second;
        float topY = baseTopY + (unsafeRestY - baseTopY) * detachT;

        float w = dropX1 - dropX0;
        float h = kMaxDropPx;
        if (w < 1.0f) continue;

        // topY is the block's origin edge (pop-out side); the window needs the true top-left corner regardless of anchor.
        float winTopY = (kDropDir > 0.0f) ? topY : (topY - h);

        char winId[48];
        snprintf(winId, sizeof(winId), "##we_subbar_click_%d", idx);

        ImGui::SetNextWindowPos(ImVec2(dropX0, winTopY));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(winId, nullptr,
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoScrollbar     |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground);
        ImGui::InvisibleButton("##we_subbar_click_hit", ImVec2(w, h));
        bool clicked      = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::End();

        if (clicked)
        {
            std::string toCopy = BuildChatPasteMessage(s.name, s.chatCode);
            PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
            io.WantCaptureMouse = true;
            break; // one click, one segment
        }

        // Right-click: mark this event/slot done for today — same
        // ToggleBasicEventDoneToday/ToggleCyclicSlotDoneToday as the
        // watchlist window's row popup. Popup ID keyed off s.key, which
        // is already unique per segment (see LineSegment::key above).
        if (rightClicked)
        {
            ImGui::OpenPopup(("##we_subbar_done_popup_" + s.key).c_str());
            io.WantCaptureMouse = true;
        }
        if (ImGui::BeginPopup(("##we_subbar_done_popup_" + s.key).c_str()))
        {
            if (ImGui::Selectable("Mark done for today"))
            {
                if (s.isBasic) ToggleBasicEventDoneToday(s.basicName);
                else           ToggleCyclicSlotDoneToday(s.cyclicKey);
            }
            ImGui::EndPopup();
        }
    }

}