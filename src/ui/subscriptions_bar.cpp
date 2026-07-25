//################################################################################
// subscriptions_bar.cpp
//--------------------------------------------------------------------------------
// RenderSubscriptionsBar()   entry point, draws the whole bar for one frame
//--------------------------------------------------------------------------------
// Draws the "Subscriptions" distribution line: a thin overlay pinned to the
// top (or bottom) edge of the screen, with one colored segment per
// subscribed event/slot across a fixed 2h window (kWindowSeconds), that
// curves into a filled colored block or detached pill under the mouse.
//
// Layered bottom-up:
//  - CollectVisibleSegments/AssignLanes turn the shared resolved-
//    subscriptions cache into LineSegments placed in local pixel space,
//    with overlapping items pushed to hidden lanes (dot markers only).
//  - PackStackRows/pillStackY lay out the vertical stack of currently
//    hovered/dropping segments, detaching into a pill when a segment
//    would otherwise cover an "unsafe" GW2-UI zone or share x-range with
//    something already occupying row 0.
//  - PathFlatBlockShoulders/FillFlatBlockShoulders/PathRoundedRect are the
//    actual geometry: a flat-top block with curved shoulders that necks
//    in and detaches into a stadium-cap pill as DropState::amount eases
//    from 0 to 1 on hover (with a click-hold override, see kClickHoldSeconds).
// RenderSubscriptionsBar ties all of this together once per frame and
// also owns the click hit-testing (copy waypoint / mark done for today).
//--------------------------------------------------------------------------------

#include "addon.h" //. SubsBarDataTimer/SubsBarDrawTimer live heres
#include "color_utils.h"
#include "events.h"
#include "events_tracking.h"
#include "imgui.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_cache.h"
#include "subscriptions_ui.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

//_ The line always spans exactly this much time from "now"; local x 0..W
// along the strip maps linearly to 0..kWindowSeconds
static constexpr int kWindowSeconds = 2 * 60 * 60;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventColorFor
//--------------------------------------------------------------------------------
// Deterministic color for a Basic Event, derived from its name (FNV-1a hash
// -> hue), since Basic Events don't carry a color of their own.
//--------------------------------------------------------------------------------
static ImU32 BasicEventColorFor(const std::string& name)
{
    unsigned int hash = 2166136261u;   //. FNV-1a 32-bit offset basis
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

//********************************************************************************
// LineSegment
//--------------------------------------------------------------------------------
// key           stable identity, e.g. "Basic:Name" or "Cyclic:Group:Offset"
// name          display name for tooltip
// chatCode      waypoint chat code pasted on click
// startX/endX   local pixel-space span, clamped to [0, W]
// active        currently underway vs upcoming
// statusSecs    secs left if active, secs until start otherwise
// durationSecs  endSec - startSec, clamped to the window; drives widest-
//               first stack ordering
// color         segment's own color (event or cyclic-group slot)
// lane          0 = drawn on the resting baseline; >0 = hidden behind
//               lane 0, shown only via dot marker + hover
// isWeekly      active-and-incomplete weekly Wizard's Vault target this
//               week (weekly_vault.h) - draws an extra small red marker
// isBasic/basicName/cyclicKey
//               identity for the right-click "Mark done for today" menu
//               (see click hit-testing near the end of this file); mirrors
//               the same trio in subscriptions_window.cpp's Row
//--------------------------------------------------------------------------------
// One drawable segment on the strip, in local pixel space (0..W).
//--------------------------------------------------------------------------------
struct LineSegment
{
    std::string key;
    std::string name;
    std::string chatCode;
    float       startX;
    float       endX;
    bool        active;
    int         statusSecs;
    int         durationSecs;
    ImU32       color;
    int         lane = 0;
    bool        isWeekly = false;

    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SegmentStatusLine
//--------------------------------------------------------------------------------
// Builds the second label line: "Active - ends in Xm YYs" or "in Xm YYs".
//--------------------------------------------------------------------------------
static std::string SegmentStatusLine(const LineSegment& seg)
{
    char buf[48];
    if (seg.active)
        snprintf(buf, sizeof(buf), "Active - ends in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    else
        snprintf(buf, sizeof(buf), "in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    return std::string(buf);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AssignLanes
//--------------------------------------------------------------------------------
// Greedy interval-graph coloring: walks segments left-to-right (already
// sorted by startX) and assigns each the lowest-numbered lane whose last
// occupant has already ended, so only one segment per point in time draws
// on the resting baseline (lane 0); everything else (lane > 0) is only
// surfaced via a dot marker + hover.
//--------------------------------------------------------------------------------
static void AssignLanes(std::vector<LineSegment>& segs)
{
    constexpr float kLaneMargin = 1.0f;   //. px of overlap tolerance
    //_ laneEndX[lane] = endX of the last segment placed in that lane
    std::vector<float> laneEndX;

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

//********************************************************************************
// DotMark
//--------------------------------------------------------------------------------
// x          baseline x position, before draw-time nudging for ties
// segIndex   index into the segs vector this dot represents
//--------------------------------------------------------------------------------
// One marker on the baseline for a hidden (lane>0) event's start tick.
//--------------------------------------------------------------------------------
struct DotMark
{
    float x;
    int   segIndex;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CollectOverlapDots
//--------------------------------------------------------------------------------
// Builds one dot per lane>0 (hidden) segment whose start tick falls inside
// the range of whatever lane-0 segment currently occupies the baseline at
// that x. A hidden segment starting in a pure gap gets no dot.
// Also includes one dot per lane-0 *weekly* segment at its own start tick -
// lane-0 segments are normally just the colored baseline line with no dot,
// but a weekly segment needs a dot regardless of lane so its red marker
// (recolored at the actual draw site below) has somewhere to render.
//--------------------------------------------------------------------------------
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

    //_ group exact-tie ticks for the nudge-apart pass at draw time
    std::sort(dots.begin(), dots.end(), [](const DotMark& a, const DotMark& b) { return a.x < b.x; });

    return dots;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CollectAllEventDots
//--------------------------------------------------------------------------------
// Minimal-mode counterpart to CollectOverlapDots: every segment (lane 0
// included) gets one dot at its own start tick, since minimal mode has no
// colored baseline line to represent lane-0 segments instead.
//--------------------------------------------------------------------------------
static std::vector<DotMark> CollectAllEventDots(const std::vector<LineSegment>& segs)
{
    std::vector<DotMark> dots;
    dots.reserve(segs.size());
    for (int i = 0; i < (int)segs.size(); i++)
        dots.push_back({ segs[i].startX, i });

    std::sort(dots.begin(), dots.end(), [](const DotMark& a, const DotMark& b) { return a.x < b.x; });

    return dots;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SegmentOverlapsUnsafeZone
//--------------------------------------------------------------------------------
// Whether a segment's x-range overlaps either configured "unsafe" margin
// (GW2's own corner UI), so its drop can start further down instead of
// covering that UI. A margin of 0 disables that side's zone.
//--------------------------------------------------------------------------------
static bool SegmentOverlapsUnsafeZone(const LineSegment& seg, float screenW)
{
    float leftZoneEnd    = (float)std::max(0, SubscriptionsBarUnsafeLeftPx);
    float rightZoneStart = screenW - (float)std::max(0, SubscriptionsBarUnsafeRightPx);

    bool inLeftZone  = leftZoneEnd  > 0.0f && seg.startX < leftZoneEnd;
    bool inRightZone = rightZoneStart < screenW && seg.endX > rightZoneStart;

    return inLeftZone || inRightZone;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CollectVisibleSegments
//--------------------------------------------------------------------------------
// Builds one LineSegment per resolved subscription (see
// subscriptions_cache.h) that overlaps the next kWindowSeconds, mapped into
// local pixel space across the given strip width. All the "what's
// subscribed / auto-tracked, is it done today, is it a weekly target"
// derivation lives in subscriptions_cache.cpp, shared with
// subscriptions_window.cpp/subscriptions_notification.cpp - this only adds
// the bar-specific parts: the kWindowSeconds clip and pixel mapping, and
// per-item color (BasicEventColorFor / CyclicGroup::SlotColor - a purely
// visual concern of this view alone, so it isn't part of the shared cache).
//--------------------------------------------------------------------------------
static std::vector<LineSegment> CollectVisibleSegments(time_t now, float stripWidth)
{
    std::vector<LineSegment> segs;
    //_ shared cache - RefreshSubscriptionsCache already ran once at the top
    // of RenderSubscriptionsBar
    const auto& resolved = GetResolvedSubscriptions();
    segs.reserve(resolved.size());

    auto secToX = [&](int sec) { return (sec / (float)kWindowSeconds) * stripWidth; };

    for (const auto& sub : resolved)
    {
        //_ "Already done today" skip (API-confirmed or manually marked) -
        // see ResolvedSubscription::doneToday.
        if (sub.doneToday) continue;

        SubscriptionActiveState as = GetSubscriptionActiveState(sub, now);

        int startSec, endSec, statusSecs;
        if (as.active)
        {
            if (as.secsUntilEnd < 0) continue; //. no timer data
            startSec   = 0; //. already underway
            endSec     = std::min(as.secsUntilEnd, kWindowSeconds);
            statusSecs = as.secsUntilEnd;
        }
        else
        {
            if (as.secsUntilStart < 0 || as.secsUntilStart >= kWindowSeconds) continue;
            startSec   = as.secsUntilStart;
            endSec     = std::min(as.secsUntilStart + sub.duration, kWindowSeconds);
            statusSecs = as.secsUntilStart;
        }

        if (endSec <= startSec) continue;
        if (as.active && SubscriptionsBarHideActive) continue;

        ImU32 color;
        if (sub.isBasic)
        {
            color = BasicEventColorFor(sub.basicName);
        }
        else
        {
            //_ resolved is already filtered to subscribed/auto-tracked
            // items, so this linear scan over g_CyclicGroups stays small.
            color = IM_COL32(255, 255, 255, 255);
            auto grpIt = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
                [&](const CyclicGroup& g) { return g.name == sub.cyclicGroupName; });
            if (grpIt != g_CyclicGroups.end())
            {
                auto slotIt = std::find_if(grpIt->slots.begin(), grpIt->slots.end(),
                    [&](const CyclicGroup::Slot& s) { return s.offset == sub.cyclicSlotOffset; });
                if (slotIt != grpIt->slots.end())
                    color = grpIt->SlotColor(*slotIt);
            }
        }

        LineSegment seg{
            sub.key, sub.label, sub.chatCode,
            secToX(startSec), secToX(endSec), as.active, statusSecs, endSec - startSec,
            color
        };
        seg.isWeekly  = sub.isWeeklyTarget;
        seg.isBasic   = sub.isBasic;
        seg.basicName = sub.basicName;
        seg.cyclicKey = CyclicSubscriptionKey{ sub.cyclicGroupName, sub.cyclicSlotOffset };
        segs.push_back(seg);
    }

    std::sort(segs.begin(), segs.end(), [](const LineSegment& a, const LineSegment& b)
    {
        return a.startX < b.startX;
    });

    AssignLanes(segs);

    return segs;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SmoothStep
//--------------------------------------------------------------------------------
// Ken Perlin's smoothstep easing.
//--------------------------------------------------------------------------------
static float SmoothStep(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FlatBlockDepthAt
//--------------------------------------------------------------------------------
// Depth profile (0..1) at local-x for a flat-top block with curved
// shoulders of width tw, confined within [start, end]: flat 0 outside the
// range, eases up across [start, start+tw], flat 1 across the middle,
// eases back down across [end-tw, end]. For segments narrower than 2*tw,
// tw is capped to half the segment's width.
//--------------------------------------------------------------------------------
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

//********************************************************************************
// DropState
//--------------------------------------------------------------------------------
// amount            current eased drop depth (0..1), toward a target
//                   driven by hover
// hoverSeconds      continuous real-hover duration; resets to 0 on losing
//                   hover
// clickHoldSeconds  counts down while a click-triggered pop-out is held
//                   open; while > 0 the segment is treated as hovered
//--------------------------------------------------------------------------------
// Per-segment eased drop animation state, keyed by LineSegment::key.
//--------------------------------------------------------------------------------
struct DropState { float amount = 0.0f; float hoverSeconds = 0.0f; float clickHoldSeconds = 0.0f; };
static std::unordered_map<std::string, DropState> s_dropStates;

//********************************************************************************
// StackRowInfo
//--------------------------------------------------------------------------------
// row           stacking row index, 0 = closest to the baseline
// rowMaxDepth   currently unused by the row-to-Y conversion (see stackTopY)
//--------------------------------------------------------------------------------
struct StackRowInfo
{
    int   row = 0;
    float rowMaxDepth = 0.0f;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PackStackRows
//--------------------------------------------------------------------------------
// Assigns each currently-hovered/dropping segment a row (0, 1, 2, ...) for
// the vertical stack, so segments whose x-ranges don't overlap can share a
// row. Processes longest-duration-first (durationSecs descending,
// soonest-first as tiebreak), so a longer-running segment wins the lowest
// available row over a shorter one it overlaps. Uses simple greedy
// interval-graph coloring, same approach as AssignLanes.
// order: indices into segs, soonest-first. dropBoundsByKey: per-key
// edge-safe [dropX0, dropX1]; overlap is checked against these, not the
// raw x-range.
//--------------------------------------------------------------------------------
static std::unordered_map<std::string, StackRowInfo> PackStackRows(
    const std::vector<LineSegment>& segs,
    const std::vector<int>& order,
    const std::unordered_map<std::string, DropState>& dropStates,
    const std::unordered_map<std::string, std::pair<float, float>>& dropBoundsByKey)
{
    constexpr float kRowMargin = 4.0f;   //. extra px between shared rows
    std::unordered_map<std::string, StackRowInfo> result;

    std::vector<int> durationOrder = order;
    std::stable_sort(durationOrder.begin(), durationOrder.end(),
        [&](int a, int b)
        {
            return segs[a].durationSecs > segs[b].durationSecs;
        });

    //_ every occupant's [startX, endX] placed in each row so far
    std::vector<std::vector<std::pair<float, float>>> rowSpans;
    //_ rowDepth[row] = max eased depth among segments placed in that row so far
    std::vector<float> rowDepth;

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

    //_ Second pass: write each row's final max depth back once all its occupants are known.
    for (int idx : order)
    {
        const LineSegment& s = segs[idx];
        auto it = result.find(s.key);
        if (it != result.end()) it->second.rowMaxDepth = rowDepth[it->second.row];
    }

    return result;
}


static constexpr float kTransitionWidth = 26.0f;   //. curved shoulder width, px
static constexpr float kEaseRate        = 0.18f;   //. per-frame lerp factor

//_ Pill-detach phase thresholds, as fractions of DropState::amount (0..1):
// grow -> neck-in at kPinchStart -> detach at kPinchEnd -> settled at kDetachEnd
static constexpr float kPinchStart = 0.60f;
static constexpr float kPinchEnd   = 0.78f;
static constexpr float kDetachEnd  = 0.92f;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// EdgeSafeDropBounds
//--------------------------------------------------------------------------------
// Widens a segment's dropped-block x-range to at least minWidth (its own
// label's required width), growing to the right first, then spilling any
// remainder left, clamped so it never crosses the opposite screen edge.
// Only ever widens - an already-wide segment is returned unchanged.
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PillPinchFactor
//--------------------------------------------------------------------------------
// 0..1 amount of inward "waist" pinch at x, for the neck-in sub-phase
// (neckT 0..1). Reuses FlatBlockDepthAt's shoulder logic centered on the
// segment's middle. Peaks mid-phase and returns to 0 at both ends of the
// sub-phase, so the shape is a plain rectangle exactly at neckT==1, ready
// for the detached-pill branch to take over.
//--------------------------------------------------------------------------------
static float PillPinchFactor(float x, float start, float end, float neckT)
{
    if (neckT <= 0.0f || neckT >= 1.0f) return 0.0f;
    float half = (end - start) * 0.5f;
    if (half <= 0.0f) return 0.0f;
    float distFromEdge = std::min(x - start, end - x) / half;   //. 0 at edge, 1 center
    float waistShape = 1.0f - std::min(1.0f, distFromEdge / 0.55f);   //. strongest away from center
    float envelope = (neckT < 0.6f) ? SmoothStep(neckT / 0.6f) : SmoothStep((1.0f - neckT) / 0.4f);
    return std::max(0.0f, waistShape * envelope);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PathFlatBlockShoulders
//--------------------------------------------------------------------------------
// Builds the flat-top/curved-shoulder silhouette as real geometry: two
// cubic Bezier splines for the rising/falling shoulders (control points
// digitized from the reference SVG's half-Gaussian curve) plus straight
// runs for the flat top and baseline. depth scales how far the flat top
// reaches from baselineY; dropDir flips the direction for bottom-anchored
// mode (+1 grows down, -1 grows up).
//--------------------------------------------------------------------------------
static void PathFlatBlockShoulders(ImDrawList* dl, float start, float end, float baselineY, float blockH, float tw, float depth, float dropDir = 1.0f)
{
    float effectiveTw = std::min(tw, (end - start) * 0.5f);
    float h = dropDir * blockH * depth;   //. signed flat-top distance from baseline

    if (effectiveTw <= 0.0f)
    {
        //_ Degenerate (very narrow segment): plain rect, no shoulders.
        dl->PathLineTo(ImVec2(start, baselineY));
        dl->PathLineTo(ImVec2(start, baselineY + h));
        dl->PathLineTo(ImVec2(end, baselineY + h));
        dl->PathLineTo(ImVec2(end, baselineY));
        return;
    }

    //_ fx/fy fractions per point: fx is 0 at the baseline corner, 1 at the flat-top
    // corner; fy is 0 at the baseline, 1 at the flat top.
    struct Pt { float fx, fy; };
    static const Pt kRise[] = {
        { 0.000f, 0.000f },                                       //. P0: baseline
        { 0.212f, 0.000f }, { 0.259f, 0.040f }, { 0.318f, 0.140f }, //. C1
        { 0.376f, 0.250f }, { 0.412f, 0.380f }, { 0.447f, 0.520f }, //. C2
        { 0.482f, 0.660f }, { 0.529f, 0.780f }, { 0.612f, 0.860f }, //. C3
        { 0.694f, 0.940f }, { 0.824f, 0.980f }, { 1.000f, 1.000f }, //. C4: flat top
    };
    constexpr int kNumPts = sizeof(kRise) / sizeof(kRise[0]);

    auto toRisePoint = [&](const Pt& p) {
        return ImVec2(start + effectiveTw * p.fx, baselineY + h * p.fy);
    };
    auto toFallPoint = [&](const Pt& p) {
        //_ Falling shoulder is the rising one mirrored horizontally, walked start-to-end.
        return ImVec2(end - effectiveTw * p.fx, baselineY + h * p.fy);
    };

    //_ fixed tessellation so rise/fall curves stay symmetric
    constexpr int kSegsPerCubic = 8;

    ImVec2 riseP0 = toRisePoint(kRise[0]);
    dl->PathLineTo(riseP0);
    for (int i = 1; i < kNumPts; i += 3)
    {
        ImVec2 cp1 = toRisePoint(kRise[i]);
        ImVec2 cp2 = toRisePoint(kRise[i + 1]);
        ImVec2 ep  = toRisePoint(kRise[i + 2]);
        dl->PathBezierCubicCurveTo(cp1, cp2, ep, kSegsPerCubic);
    }

    //_ Flat block top, drawn explicitly to the fall spline's own start point.
    ImVec2 flatEnd = toFallPoint(kRise[kNumPts - 1]);
    dl->PathLineTo(flatEnd);

    //_ Falling shoulder: same spline walked back-to-front, mirrored via toFallPoint.
    for (int i = kNumPts - 4; i >= 0; i -= 3)
    {
        ImVec2 cp1 = toFallPoint(kRise[i + 2]);
        ImVec2 cp2 = toFallPoint(kRise[i + 1]);
        ImVec2 ep  = toFallPoint(kRise[i]);
        dl->PathBezierCubicCurveTo(cp1, cp2, ep, kSegsPerCubic);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FillFlatBlockShoulders
//--------------------------------------------------------------------------------
// Fills the exact silhouette PathFlatBlockShoulders() strokes, as a single
// triangulated mesh (center rect + two shoulder-cap fans, sharing vertex
// indices at both seams) so there is no crack between separately-drawn
// pieces. Tessellates the rise/fall curves via the same calls
// PathFlatBlockShoulders uses, so the fill matches the stroke point-for-point.
//--------------------------------------------------------------------------------
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
    constexpr int kSegsPerCubic = 8; //. must match PathFlatBlockShoulders

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

    //_ One combined vertex buffer (rise, fall, two inner-top corners); every
    // triangle indexes into it, including at the rect/cap seams, so shared
    // edges reuse the same vertex rather than duplicating it.
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    int vtxCount = riseN + fallN + 2;
    //_ left fan + right fan + rect (2 tris)
    int triCount = (riseN - 1) + (fallN - 1) + 2;
    dl->PrimReserve(triCount * 3, vtxCount);

    unsigned int base = dl->_VtxCurrentIdx;
    for (int i = 0; i < riseN; i++) { dl->_VtxWritePtr->pos = risePts[i]; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++; }
    for (int i = 0; i < fallN; i++) { dl->_VtxWritePtr->pos = fallPts[i]; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++; }
    dl->_VtxWritePtr->pos = innerTopLeft;  dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++;
    dl->_VtxWritePtr->pos = innerTopRight; dl->_VtxWritePtr->uv = uv; dl->_VtxWritePtr->col = fillColor; dl->_VtxWritePtr++;
    dl->_VtxCurrentIdx += (unsigned int)vtxCount;

    unsigned int idxRise0        = base;
    unsigned int idxRiseInner    = base + (riseN - 1); //. L_bottom
    unsigned int idxFall0        = base + riseN;
    unsigned int idxFallInner    = base + riseN + (fallN - 1); //. R_bottom
    unsigned int idxInnerTopLeft  = base + riseN + fallN;      //. L_top
    unsigned int idxInnerTopRight = base + riseN + fallN + 1;  //. R_top

    auto tri = [&](unsigned int a, unsigned int b, unsigned int c)
    {
        dl->_IdxWritePtr[0] = (ImDrawIdx)a; dl->_IdxWritePtr[1] = (ImDrawIdx)b; dl->_IdxWritePtr[2] = (ImDrawIdx)c;
        dl->_IdxWritePtr += 3;
    };

    //_ Left cap: fan from its inner-top corner across the rise curve's points.
    for (int i = 0; i < riseN - 1; i++)
        tri(idxInnerTopLeft, idxRise0 + i, idxRise0 + i + 1);

    //_ Right cap: mirror, fan from its own inner-top corner.
    for (int i = 0; i < fallN - 1; i++)
        tri(idxInnerTopRight, idxFall0 + i, idxFall0 + i + 1);

    //_ Center rectangle, as 2 more triangles in the same mesh as the caps.
    tri(idxInnerTopLeft, idxInnerTopRight, idxFallInner);
    tri(idxInnerTopLeft, idxFallInner, idxRiseInner);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PathRoundedRect
//--------------------------------------------------------------------------------
// Rounded-rect path via direct PathArcTo calls, with a radius-scaled
// segment count, so a full stadium cap (rx = height/2) renders smoothly
// instead of AddRect's fixed 3-segments-per-corner faceting. Same
// winding/geometry as AddRect (left side bottom-left->top-left arcs, right
// side top-right->bottom-right arcs), so it's a drop-in replacement.
//--------------------------------------------------------------------------------
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
    dl->PathArcTo(ImVec2(x0 + rounding, y1 - rounding), rounding, kPi * 0.5f, kPi,        segsPerQuarter); //. bottom-left
    dl->PathArcTo(ImVec2(x0 + rounding, y0 + rounding), rounding, kPi,        kPi * 1.5f, segsPerQuarter); //. top-left
    dl->PathArcTo(ImVec2(x1 - rounding, y0 + rounding), rounding, kPi * 1.5f, kPi * 2.0f, segsPerQuarter); //. top-right
    //_ Starts at "kPi * 2.0f", not the equivalent 0.0f, so cosf/sinf match
    // top-right's end point bit-for-bit and avoid a few-ULP seam gap.
    dl->PathArcTo(ImVec2(x1 - rounding, y1 - rounding), rounding, kPi * 2.0f, kPi * 2.5f, segsPerQuarter); //. bottom-right
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsBar
//--------------------------------------------------------------------------------
// Entry point, called once per frame from AddonRender (addon.cpp) whenever
// IsGameplay is true. In order: resolve segments (CollectVisibleSegments),
// lay out dots, track hover and ease each segment's drop depth, draw the
// baseline and dot markers, pack the vertical stack of open segments, draw
// each segment's block/pill, then two click-hit-test passes (pop out via
// the thin line, and copy-waypoint/mark-done on an already-dropped block).
//--------------------------------------------------------------------------------
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
    float screenH = io.DisplaySize.y;   //. only used by SubscriptionsBarBottomAnchored
    if (screenW <= 0.0f) return;

    time_t now = time(nullptr);
    std::vector<LineSegment> segs;
    {
        //_ Scoped to just data gathering/resolution (RefreshSubscriptionsCache
        // is a no-op most frames). Split from SubsBarDrawTimer below so the
        // debug line can separate re-deriving cost from rendering cost.
        SubsBarDataTimer dataTimer;
        RefreshSubscriptionsCache(now);
        segs = CollectVisibleSegments(now, screenW);
    }
    if (segs.empty())
    {
        s_dropStates.clear();
        return;
    }
    //_ Everything below: dot layout, hover detection, and the actual ImGui
    // draw-list calls - see g_AvgSubsBarDrawMs's comment in addon.h.
    SubsBarDrawTimer drawTimer;
    //_ Minimal mode hides the per-segment colored baseline, so every event
    // needs its own dot rather than just the hidden lane>0 ones.
    std::vector<DotMark> dots = SubscriptionsBarMinimalMode
        ? CollectAllEventDots(segs)
        : CollectOverlapDots(segs, screenW);

    //_ Layout constants below are in local space: y=0 is the baseline strip.
    constexpr float kLineThick    = 2.0f;
    //_ Flips every depth offset so blocks/pills/dots grow up off the bottom
    // edge when bottom-anchored, instead of down off the top edge.
    const float kDropDir = SubscriptionsBarBottomAnchored ? -1.0f : 1.0f;
    const float kBaselineY = SubscriptionsBarBottomAnchored
        ? (screenH - kLineThick * 0.5f)
        : (kLineThick * 0.5f);
    //_ floored so pill radius math stays positive
    const float kMaxDropPx = (float)std::max(8, SubscriptionsBarMaxDropPx);
    constexpr float kGapPx        = 3.0f;  //. notch between adjacent segments
    constexpr float kStackGapPx   = 4.0f;  //. gap between dropped blocks
    constexpr float kDotRadius    = 2.5f;  //. px
    constexpr float kDotSpacingPx = 7.0f;  //. spacing between tied dots
    //_ Normal mode offsets dots below the colored line; minimal mode has no
    // colored line, so its dots sit directly on the baseline instead.
    const float kDotY = SubscriptionsBarMinimalMode ? kBaselineY : (kBaselineY + kDropDir * 8.0f);
    constexpr float kDotHitRadius = 5.0f;  //. hit-test radius around each dot
    //_ used for the minimum segment width below
    constexpr float kLabelPadX    = 6.0f;

    //_ Nudged draw-x per dot, so same-tick dots spread into a small cluster
    // instead of overlapping.
    std::vector<float> dotDrawX(dots.size());
    {
        size_t i = 0;
        while (i < dots.size())
        {
            size_t j = i;
            while (j < dots.size() && fabsf(dots[j].x - dots[i].x) < 0.5f) j++; //. group exact-tie ticks
            size_t groupCount = j - i;
            float groupCenter = dots[i].x;
            float groupStart  = groupCenter - (groupCount - 1) * kDotSpacingPx * 0.5f;
            for (size_t k = i; k < j; k++) dotDrawX[k] = groupStart + (k - i) * kDotSpacingPx;
            i = j;
        }
    }

    ImVec2 mouse = io.MousePos;
    //_ ImGui reports (-FLT_MAX,-FLT_MAX) when there's no mouse
    bool mouseValid = io.MousePos.x > -FLT_MAX;

    //_ Segments under the mouse, via two paths: (1) a lane-0 segment's own
    // x-range within a shared hover band, so the mouse can travel onto an
    // open block without losing hover; (2) a dot marker (both feed hoveredIndices).
    constexpr float kLineHalfHeight = (kLineThick + 1.0f) * 0.5f;
    //_ the only band that can start a fresh pop-out
    constexpr float kHoverBand = kLineHalfHeight + 2.0f;

    //_ Shared sustain-band far edge: baseline by default, pushed out along
    // kDropDir to cover whatever's currently deepest (estimated from last
    // frame's state - one row per open segment, an upper bound on height).
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
                if (segs[i].lane != 0) continue; //. lane>0 segments have no line of their own to hover
                if (mouse.x >= segs[i].startX && mouse.x < segs[i].endX) hoveredIndices.push_back(i);
            }
        }
        //_ Sustain path: segments already mid-drop, tested against the
        // shared band (near edge here, far edge sustainBottom, padded by
        // kSustainPadX) so a dot-triggered open survives leaving the hit-circle.
        float sustainNear = kBaselineY - kDropDir * kHoverBand;
        float sustainLo = std::min(sustainNear, sustainBottom);
        float sustainHi = std::max(sustainNear, sustainBottom);
        if (mouse.y >= sustainLo && mouse.y <= sustainHi)
        {
            //_ px of forgiveness around the opened element's own width
            constexpr float kSustainPadX = 8.0f;
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
    //_ Fold in any segment held open by a click (see DropState::clickHoldSeconds).
    for (int i = 0; i < (int)segs.size(); i++)
    {
        if (s_dropStates[segs[i].key].clickHoldSeconds <= 0.0f) continue;
        if (std::find(hoveredIndices.begin(), hoveredIndices.end(), i) == hoveredIndices.end())
            hoveredIndices.push_back(i);
    }
    //_ Stacking order: soonest-starting (or active) segment on top. Stable
    // sort (key as final tiebreak) so exact statusSecs ties don't reorder
    // frame to frame, which would also destabilize PackStackRows' own tiebreak.
    std::stable_sort(hoveredIndices.begin(), hoveredIndices.end(), [&](int a, int b)
    {
        if (segs[a].statusSecs != segs[b].statusSecs) return segs[a].statusSecs < segs[b].statusSecs;
        return segs[a].key < segs[b].key;
    });

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    //_ Track raw hover duration, then ease every segment's drop amount
    // toward a delay-gated target.
    float dt = io.DeltaTime > 0.0f ? std::min(io.DeltaTime, 0.1f) : 0.0f;
    //_ rate raised to a power of (dt*60) keeps the same half-life at any fps
    float easeThisFrame = 1.0f - powf(1.0f - kEaseRate, dt > 0.0f ? dt * 60.0f : 1.0f);
    float hoverDelaySeconds = std::max(0, SubscriptionsBarHoverDelayMs) / 1000.0f;

    for (int i = 0; i < (int)segs.size(); i++)
    {
        DropState& st = s_dropStates[segs[i].key];

        //_ clickHoldSeconds forces hovered regardless of real mouse position;
        // forcePastDelay only applies if the click had already cleared this
        // segment's own hover delay through real dwell time (see below).
        bool isHovered = std::find(hoveredIndices.begin(), hoveredIndices.end(), i) != hoveredIndices.end();
        bool forcePastDelay = false;
        if (st.clickHoldSeconds > 0.0f)
        {
            isHovered = true;
            forcePastDelay = true;
            st.clickHoldSeconds = std::max(0.0f, st.clickHoldSeconds - dt);
        }

        //_ Raw hover duration resets instantly on losing hover.
        st.hoverSeconds = isHovered ? (st.hoverSeconds + dt) : 0.0f;

        //_ Drop targets 1.0 only once hover clears the configured delay;
        // losing hover always targets 0 immediately, with no delay back down.
        bool pastDelay = isHovered && (forcePastDelay || st.hoverSeconds >= hoverDelaySeconds);
        float target = pastDelay ? 1.0f : 0.0f;

        st.amount += (target - st.amount) * easeThisFrame;
        if (fabsf(st.amount - target) < 0.001f) st.amount = target;
    }

    //_ Drop stale keys (segment no longer visible this frame) so the map
    // doesn't grow unbounded.
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

    //_ Thin ambient rail across the full screen width, drawn in both modes.
    dl->AddLine(ImVec2(0, kBaselineY), ImVec2(screenW, kBaselineY),
        IM_COL32(255, 255, 255, 90), kLineThick);

    //_ Dot markers: white, red for an active-and-incomplete weekly Wizard's
    // Vault target (see weekly_vault.h/.cpp for isWeekly), fading out as
    // their own segment drops in.
    for (size_t d = 0; d < dots.size(); d++)
    {
        const LineSegment& dotSeg = segs[dots[d].segIndex];
        float depth = s_dropStates[dotSeg.key].amount;
        float alpha = 1.0f - depth;
        if (alpha <= 0.02f) continue;

        ImVec4 c = ToImVec4(dotSeg.isWeekly ? WeeklyAutoTrackColor : SubscriptionsBarDotColor);
        //_ keep the 235 alpha cap, scaled by fade
        ImU32 dotColor = FadeU32(c, (235.0f / 255.0f) * alpha);

        dl->AddCircleFilled(ImVec2(dotDrawX[d], kDotY), kDotRadius, dotColor, 12);
    }

    //_ Top-of-block Y for each hovered segment, filled in by the row-packing
    // below; drops always grow from the baseline, moving toward unsafe-zone
    // clearance only once fully detached into a pill.
    std::unordered_map<std::string, float> stackTopY;

    //_ Edge-safe drop bounds, precomputed per segment so row-packing,
    // drawing, and click hit-testing all agree.
    std::unordered_map<std::string, std::pair<float, float>> dropBoundsByKey;
    dropBoundsByKey.reserve(segs.size());
    for (const auto& seg : segs)
    {
        float bx0 = seg.startX;
        float bx1 = seg.endX;
        if (seg.endX < screenW) bx1 -= kGapPx;
        if (bx1 <= bx0) bx1 = bx0 + 1.0f;

        //_ Minimum drop width derived from this segment's own label text
        // + plate padding.
        ImVec2 nameSize   = ImGui::CalcTextSize(seg.name.c_str());
        ImVec2 statusSize = ImGui::CalcTextSize(SegmentStatusLine(seg).c_str());
        float minWidth = std::max(nameSize.x, statusSize.x) + kLabelPadX * 2.0f;

        float dropX0, dropX1;
        EdgeSafeDropBounds(bx0, bx1, screenW, minWidth, dropX0, dropX1);
        dropBoundsByKey[seg.key] = { dropX0, dropX1 };
    }

    std::unordered_map<std::string, StackRowInfo> stackRows = PackStackRows(segs, hoveredIndices, s_dropStates, dropBoundsByKey);

    {
        int maxRow = -1;
        for (auto& kv : stackRows) maxRow = std::max(maxRow, kv.second.row);

        //_ Row height is NOT scaled by live eased depth (an open row and a
        // still-easing row below must never overlap), so every occupied
        // row reserves full kMaxDropPx unconditionally.
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

    //_ Per-segment resting Y for detached pills, so multiple simultaneously-
    // open pills stagger below the configured clearance instead of
    // converging on the same absolute Y.
    std::unordered_map<std::string, float> pillStackY;
    {
        //_ Base clearance is the larger of the configured unsafe-zone
        // clearance and "past every attached row above the first pill row"
        // (a pill can't rest inside an ordinary block in the same space).
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

        //_ "Farther along kDropDir": larger y when top-anchored, smaller y when bottom-anchored.
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

        //_ Keyed by row (not by segment): segments already packed into the
        // same row don't overlap in x, so they share one pill Y slot.
        struct PillCandidate { int row; std::string key; };
        std::vector<PillCandidate> candidates;
        //_ negative space, clear of any real row index
        int nextSyntheticRow = -1000000;

        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto rowIt = stackRows.find(s.key);
            bool stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
            if (!inUnsafeZone && !stackDetach) continue;   //. only pills need a slot

            float depth = s_dropStates[s.key].amount;
            float detachT = std::min(1.0f, std::max(0.0f, (depth - kPinchEnd) / (kDetachEnd - kPinchEnd)));
            if (detachT <= 0.0f) continue;   //. not detaching yet

            int row = (rowIt != stackRows.end()) ? rowIt->second.row : nextSyntheticRow--;
            candidates.push_back({row, s.key});
        }

        //_ Sort by row so slots are handed out row 0, then row 1, etc.,
        // regardless of hover-order.
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

    //_ Colored baseline overlay is lane-0 only; the block/pill drop
    // applies to any lane.
    for (int i = 0; i < (int)segs.size(); i++)
    {
        const LineSegment& seg = segs[i];
        float depth = s_dropStates[seg.key].amount;

        //_ The resting baseline line always uses the segment's true x-range;
        // only the dropped block/pill below uses the edge-safe widened bounds.
        float x0 = seg.startX;
        float x1 = seg.endX;
        float segEnd = x1;
        //_ small gap so adjacent segments read as distinct blocks
        if (seg.endX < screenW) segEnd -= kGapPx;
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        ImU32 segColor = seg.color;
        //_ Nexus's own WindowBg (ThemeColorU32, shared with
        // subscriptions_notification.cpp), not segColor - so the pop-out
        // reads as UI chrome, not a solid tint of the event's own color.
        ImU32 fillColor = ThemeColorU32(ImGuiCol_WindowBg, 0.25f + 0.75f * (seg.active ? 1.0f : 0.7f));

        if (seg.lane == 0 && !SubscriptionsBarMinimalMode)
        {
            //_ Colored resting line - only lane-0 segments get one; overlap
            // is signaled by dots instead.
            dl->AddLine(ImVec2(x0, kBaselineY), ImVec2(segEnd, kBaselineY), segColor, kLineThick + 1.0f);
        }

        if (depth > 0.002f)
        {
            //_ Stacked target Y: this segment's reserved slot if hovered,
            // otherwise the baseline (mid-ease-out, no resting Y of its own).
            float topY = kBaselineY;
            auto stackIt = stackTopY.find(seg.key);
            if (stackIt != stackTopY.end()) topY = stackIt->second;

            //_ Edge-safe drop bounds (only affect the drop shape, not the
            // baseline line above).
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

            //_ A segment detaches into a pill if it overlaps an unsafe zone,
            // or if it's not in row 0 of the current stack.
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(seg, screenW);
            bool stackDetach = false;
            {
                auto rowIt = stackRows.find(seg.key);
                stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
            }
            bool shouldDetach = inUnsafeZone || stackDetach;

            //_ Phase split (see kPinchStart/kPinchEnd/kDetachEnd): grow ->
            // neck-in -> detach into a locked-width pill -> settle.
            float pinchT  = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchStart) / (kPinchEnd - kPinchStart))) : 0.0f;
            float detachT = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchEnd)   / (kDetachEnd - kPinchEnd))) : 0.0f;

            float blockH = kMaxDropPx;
            //_ Corner radius eases from 0 (attached) to a stadium cap (rx = h/2) as detachT completes.
            float pillRx = (blockH * 0.5f) * detachT;

            float unsafeRestY = kBaselineY + kDropDir * (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
            auto pillIt = pillStackY.find(seg.key);
            if (pillIt != pillStackY.end()) unsafeRestY = pillIt->second;
            //_ Eases from topY down to this segment's reserved pillStackY slot as it detaches.
            float pillY = topY + (unsafeRestY - topY) * detachT;

            if (depth < kPinchEnd || !shouldDetach)
            {
                //_ Attached FlatBlockDepthAt silhouette; shoulders neck in
                // via PillPinchFactor once past kPinchStart.
                float tw = kTransitionWidth;

                if (pinchT <= 0.0f)
                {
                    //_ Common case: real bezier-curve shoulders via
                    // PathFlatBlockShoulders/FillFlatBlockShoulders.
                    FillFlatBlockShoulders(dl, dropX0, dropX1, topY, blockH, tw, depth, fillColor, kDropDir);

                    dl->PathClear();
                    PathFlatBlockShoulders(dl, dropX0, dropX1, topY, blockH, tw, depth, kDropDir);
                    dl->PathStroke(segColor, false, kLineThick);
                }
                else
                {
                    //_ Neck-in sub-phase: PillPinchFactor's waist reshapes
                    // every frame, so this samples FlatBlockDepthAt point-
                    // by-point instead of using the fixed bezier path.
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
                //_ Detached: a rounded rect locked to the segment's own
                // on-bar width, corner radius eased via pillRx. Assumes
                // p0.y < p1.y; order by min/max since pillY can be either edge.
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

            //_ Label + status, vertically centered inside the block/pill's
            // own slice of the stack, fading in with depth.
            if (depth > 0.35f)
            {
                std::string line1 = seg.name;
                std::string line2 = SegmentStatusLine(seg);
                ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
                ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());

                //_ blockNear is whichever edge sits at the pop-out origin
                // (topY or pillY); blockFar is the opposite edge.
                float blockNear   = (depth < kPinchEnd || !shouldDetach) ? topY : pillY;
                float blockFar    = blockNear + kDropDir * blockH;
                float blockTop    = std::min(blockNear, blockFar);
                float blockBottom = std::max(blockNear, blockFar);
                float textBlockH  = size1.y + size2.y;
                float labelY      = blockTop + (blockBottom - blockTop - textBlockH) * 0.5f;

                float alpha = (depth - 0.35f) / 0.65f;
                ImU32 textCol = ThemeColorU32(ImGuiCol_Text, alpha * (230.0f / 255.0f));

                //_ No separate backing plate: the block/pill fill drawn
                // above is already Nexus's own WindowBg (see fillColor
                // above), so a second plate here would just be redundant.

                dl->AddText(ImVec2(cx - size1.x * 0.5f, labelY), textCol, line1.c_str());
                dl->AddText(ImVec2(cx - size2.x * 0.5f, labelY + size1.y), textCol, line2.c_str());
            }
        }
    }

    //_ Click-on-the-thin-line: pop out every segment overlapping mouse.x.
    // Both click blocks below use an invisible window + InvisibleButton,
    // since a plain IsMouseClicked() doesn't fire on this draw list.
    {
        //_ small padding on the bottom-anchored window to catch the bottom-most row
        constexpr float kBottomEdgeSlackPx = 4.0f;
        float lineWinY0 = std::max(SubscriptionsBarBottomAnchored ? -FLT_MAX : 0.0f, kBaselineY - kHoverBand);
        float lineWinY1 = SubscriptionsBarBottomAnchored
            ? (screenH + kBottomEdgeSlackPx)
            : std::min(FLT_MAX, kBaselineY + kHoverBand);

        ImGui::SetNextWindowPos(ImVec2(0, lineWinY0));
        ImGui::SetNextWindowSize(ImVec2(screenW, lineWinY1 - lineWinY0));
        ImGui::SetNextWindowBgAlpha(0.0f);
        //_ Default WindowPadding would offset the clickable rect from the
        // actual line.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        //_ NoBringToFrontOnFocus deliberately omitted: letting a click
        // bring this window to front keeps its z-order self-healing.
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
                //_ Require the clicked segment to have already cleared the
                // hover delay through real dwell time before a click does anything.
                bool clickedPastDelay = clickedIdx >= 0 &&
                    s_dropStates[segs[clickedIdx].key].hoverSeconds >= hoverDelaySeconds;
                if (clickedPastDelay)
                {
                    //_ how long a click-triggered pop-out holds before easing back down
                    constexpr float kClickHoldSeconds = 2.0f;
                    //_ Trigger every segment whose own x-range contains
                    // mouse.x (not just the whole clicked span), lane>0 included.
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

    //_ Click hit-testing: click an already-dropped block or pill to copy
    // its waypoint. Left-click copies the code (only segments that have
    // finished dropping are eligible); right-click marks it done for today.
    for (int idx : hoveredIndices)
    {
        const LineSegment& s = segs[idx];
        float depth = s_dropStates[s.key].amount;
        if (depth <= 0.5f) continue;   //. not visibly dropped yet

        float x0 = s.startX;
        float segEnd = s.endX;
        if (s.endX < screenW) segEnd -= kGapPx;
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        //_ Edge-safe bounds from dropBoundsByKey, so the click window
        // matches what was actually drawn/row-packed.
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

        //_ topY is the block's origin edge (pop-out side); the window
        // needs the true top-left corner regardless of anchor.
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
            break;   //. one click, one segment
        }

        //_ Right-click: mark this event/slot done for today - same
        // ToggleBasicEventDoneToday/ToggleCyclicSlotDoneToday as the
        // watchlist window's row popup. Popup ID keyed off s.key (unique).
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