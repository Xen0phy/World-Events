// subscriptions_bar.cpp
// Draws the "Subscriptions" distribution LINE: a thin overlay pinned to
// the top edge of the screen, one colored segment per subscribed
// event/slot across a fixed 2h window, that curves into a filled colored
// block under the mouse. Port of the reference distribution-line.html
// mock's curve-drop hover animation onto ImGui's background draw list.

#include "subscriptions_bar.h"
#include "subscriptions.h"
#include "events.h"
#include "cyclic.h"
#include "maprender.h"
#include "settings.h"
#include "imgui.h"
// TEMPORARY (this session): addon.h pulls in Nexus.h -> <windows.h>, which
// makes this file no longer syntax-checkable with the sandbox's
// `g++ -fsyntax-only` command (see the handoff's "Environment / build
// gotchas" section — same constraint addon.cpp/addon_options.cpp already
// live with). Only added for APIDefs->Log() debug output below
// (kDebugLogStackPacking). Remove this include and the debug block once
// the row-packing bug report is diagnosed — don't leave it in permanently
// just because it compiles fine on the real Windows build.
#include "addon.h"
#include <ctime>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>
#include <unordered_map>
#include <climits>
#include <algorithm>

// ---------------------------------------------------------------------------
// kWindowSeconds
// ---------------------------------------------------------------------------
// The line always represents exactly this much time, starting at "now" —
// fixed, not a setting, per the call made this session (see
// subscriptions_bar.h). Local x-coordinate 0..W along the strip maps
// linearly to 0..kWindowSeconds.
// ---------------------------------------------------------------------------
static constexpr int kWindowSeconds = 2 * 60 * 60; // 2 hours

// ---------------------------------------------------------------------------
// BasicEventColorFor
// ---------------------------------------------------------------------------
// Basic Events (events.h) don't carry a color of their own — on the map
// they're drawn with the shared BasicEventColorActive/Soon/Waiting status
// colors instead (maprender.cpp), which all Basic Events share alike.
// That scheme doesn't help here: this line needs each DIFFERENT
// subscribed event to be visually distinguishable from its neighbors on
// the same strip, the same way Cyclic slots already are via SlotColor().
// So each Basic Event instead gets a color deterministically derived
// from its own name (FNV-1a hash -> hue), stable across frames/sessions
// without being stored anywhere.
// ---------------------------------------------------------------------------
static ImU32 BasicEventColorFor(const std::string& name)
{
    unsigned int hash = 2166136261u; // FNV-1a 32-bit
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

// ---------------------------------------------------------------------------
// LineSegment
// ---------------------------------------------------------------------------
// One drawable segment on the strip, in LOCAL space (localX 0..W, where
// W is the on-screen strip width in pixels — the 2h window is mapped
// linearly onto it). startX/endX already clamped to [0, W]: startX may
// be 0 (occurrence started before "now" and is still active), endX is
// always > startX and <= W.
//
// `key` is a stable per-subscription identity string used to track each
// segment's own eased hover-drop amount across frames (see
// s_dropAmount below) — same purpose as the flash-key in
// subscriptions_window.cpp, but persistent per-frame animation state
// instead of a one-shot click flash.
// ---------------------------------------------------------------------------
struct LineSegment
{
    std::string key;      // stable identity, e.g. "Basic:Name" or "Cyclic:Group:Offset"
    std::string name;      // display name for tooltip
    std::string chatCode;
    float       startX;
    float       endX;
    bool        active;
    int         statusSecs; // secs left if active, secs until start otherwise
    int         durationSecs; // endSec - startSec (clamped to window) — the STABLE integer-seconds source for PackStackRows' widest-first ordering. Using endX-startX directly there was tried first but flickered: two segments with near-identical on-screen pixel width could reorder frame-to-frame from float jitter as the strip re-renders, even though their underlying durations never change. durationSecs is set once here from the same startSec/endSec already computed above (clamped to kWindowSeconds, so an "active, ends off the right edge of the window" segment reports its CLAMPED remaining/visible duration, not its full real-world duration — deliberately consistent with startX/endX also being clamped, so "widest" still means "widest as drawn on screen right now", just without the float-precision flicker).
    ImU32       color;
    int         lane = 0;   // 0 = this segment is the one drawn on the single resting baseline for its time range; >0 = this segment is currently hidden behind a lane-0 segment and only shows up as a dot marker + on hover — see AssignLanes
};

// ---------------------------------------------------------------------------
// SegmentStatusLine
// ---------------------------------------------------------------------------
// The second label line ("Active - ends in 5m 30s" / "in 12m 04s"). Used in
// two places: the draw loop's actual label text, and the edge-safe drop
// bounds precompute (which needs to measure this exact string's width up
// front to derive the minimum drop width — see EdgeSafeDropBounds' and the
// dropBoundsByKey precompute loop's comments). Factored out so those two
// call sites can't drift apart and silently disagree on what text is being
// measured vs. what's actually drawn.
// ---------------------------------------------------------------------------
static std::string SegmentStatusLine(const LineSegment& seg)
{
    char buf[48];
    if (seg.active)
        snprintf(buf, sizeof(buf), "Active - ends in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    else
        snprintf(buf, sizeof(buf), "in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// AssignLanes
// ---------------------------------------------------------------------------
// Two subscriptions can easily land at the same time (e.g. two Cyclic
// bosses that spawn together, or a Basic Event overlapping a subscribed
// slot). Rather than drawing extra permanent lines for that (which ate
// screen space at rest — see the earlier iteration of this file), the
// resting baseline only ever draws ONE segment per point in time: the
// lane-0 segment. Every other overlapping segment (lane > 0) draws no
// line of its own at rest — it's only surfaced via a small dot marker at
// its start tick (see DotTickSeconds/collect dots below) and via the
// normal hover-drop, exactly like a lane-0 segment.
//
// This does simple greedy interval-graph coloring: walk segments
// left-to-right, and for each one assign the lowest-numbered lane whose
// most-recently-placed segment has already ended (with a hair of
// x-margin so near-misses still separate visually). Segments assumed
// already sorted by startX (CollectVisibleSegments sorts right before
// calling this).
// ---------------------------------------------------------------------------
static void AssignLanes(std::vector<LineSegment>& segs)
{
    constexpr float kLaneMargin = 1.0f; // px — treat near-touching segments as overlapping too, not just strictly-overlapping ones
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

// ---------------------------------------------------------------------------
// DotMark
// ---------------------------------------------------------------------------
// One small marker on the baseline: "a hidden (lane>0) event starts at
// this 5-minute tick, and it overlaps whatever's currently shown there".
// segIndex points back into the same segs vector CollectOverlapDots was
// given, so hovering/clicking a dot can drive the exact same drop
// animation and click-to-copy as hovering that segment's own line would.
// ---------------------------------------------------------------------------
struct DotMark
{
    float x;        // baseline x position of this tick (unnudged — nudging for co-occurring dots happens at draw time)
    int   segIndex; // index into segs of the hidden event this dot represents
};

// ---------------------------------------------------------------------------
// CollectOverlapDots
// ---------------------------------------------------------------------------
// GW2 events always start on 5-minute marks, so "does a hidden event
// start here" only ever needs checking at 5-minute ticks, not every
// second. For every lane>0 (hidden) segment, this checks whether its
// start tick falls inside the time range of whatever lane-0 segment is
// currently occupying the baseline at that x — if so, that's exactly
// "here starts another event which is overlapping with a displayed
// event", and gets a dot. A hidden segment whose start doesn't land
// inside any lane-0 segment's range (e.g. it starts in a pure gap on the
// baseline) gets no dot — there's nothing being hidden to flag.
// ---------------------------------------------------------------------------
static std::vector<DotMark> CollectOverlapDots(const std::vector<LineSegment>& segs, float stripWidth)
{
    std::vector<DotMark> dots;

    // Bucket the lane-0 segments so "is x inside a currently-shown
    // segment" is a simple scan rather than needing a full timeline
    // reconstruction — there are only ever a handful of subscriptions
    // visible in a 2h window, so this stays cheap.
    std::vector<int> lane0Indices;
    for (int i = 0; i < (int)segs.size(); i++)
        if (segs[i].lane == 0) lane0Indices.push_back(i);

    for (int i = 0; i < (int)segs.size(); i++)
    {
        if (segs[i].lane == 0) continue; // only hidden segments need a dot at all

        float startX = segs[i].startX;
        for (int lane0Idx : lane0Indices)
        {
            const LineSegment& shown = segs[lane0Idx];
            if (startX >= shown.startX && startX < shown.endX)
            {
                dots.push_back({ startX, i });
                break; // one dot per hidden segment's start, even if it somehow matched more than one shown range
            }
        }
    }

    // Sort by x, then group ties (multiple hidden events starting on the
    // exact same tick) together for the horizontal nudge-apart pass at
    // draw time — see the render loop's kDotSpacingPx use below.
    std::sort(dots.begin(), dots.end(), [](const DotMark& a, const DotMark& b) { return a.x < b.x; });

    return dots;
}

// ---------------------------------------------------------------------------
// SegmentOverlapsUnsafeZone
// ---------------------------------------------------------------------------
// GW2's own UI lives in the top-left (party/buffs) and top-right
// (minimap/compass) corners — there's only a handful of px of genuinely
// free space directly under the line there, nowhere near enough for a
// dropped block to sit without covering something. The wide middle
// strip of the screen has real free space underneath it. This checks
// whether a segment's x-range falls (even partially) inside either
// configured unsafe margin, so its drop can start further down instead
// of from the line itself — see SubscriptionsBarUnsafeLeftPx /
// SubscriptionsBarUnsafeRightPx's comments in settings_table.h. A
// setting of 0 disables that side's zone entirely (segment can never
// overlap a zero-width zone).
// ---------------------------------------------------------------------------
static bool SegmentOverlapsUnsafeZone(const LineSegment& seg, float screenW)
{
    float leftZoneEnd    = (float)std::max(0, SubscriptionsBarUnsafeLeftPx);
    float rightZoneStart = screenW - (float)std::max(0, SubscriptionsBarUnsafeRightPx);

    bool inLeftZone  = leftZoneEnd  > 0.0f && seg.startX < leftZoneEnd;
    bool inRightZone = rightZoneStart < screenW && seg.endX > rightZoneStart;

    return inLeftZone || inRightZone;
}

// ---------------------------------------------------------------------------
// Walks the same two subscription lists RenderSubscriptionsWindow does
// (g_SubscribedBasicEvents / g_SubscribedCyclicSlots) and produces one
// LineSegment per occurrence that overlaps the next kWindowSeconds,
// already mapped into local pixel space across the given strip width.
// ---------------------------------------------------------------------------
static std::vector<LineSegment> CollectVisibleSegments(time_t now, float stripWidth)
{
    std::vector<LineSegment> segs;
    segs.reserve(g_SubscribedBasicEvents.size() + g_SubscribedCyclicSlots.size());

    auto secToX = [&](int sec) { return (sec / (float)kWindowSeconds) * stripWidth; };

    for (const auto& evName : g_SubscribedBasicEvents)
    {
        auto it = std::find_if(g_Events.begin(), g_Events.end(),
            [&](const WorldEvent& ev) { return ev.name == evName; });
        if (it == g_Events.end()) continue; // deleted since subscribing

        bool active = IsEventActive(*it, now);
        int  startSec, endSec, statusSecs;

        if (active)
        {
            int secsLeft = GetSecondsUntilEventEnd(*it, now);
            if (secsLeft < 0) continue; // no timer data
            startSec   = 0; // already underway — clip to the left/"now" edge
            endSec     = std::min(secsLeft, kWindowSeconds);
            statusSecs = secsLeft;
        }
        else
        {
            int secsUntilStart = GetSecondsUntilEventStart(*it, now);
            if (secsUntilStart < 0 || secsUntilStart >= kWindowSeconds) continue;
            startSec   = secsUntilStart;
            endSec     = std::min(secsUntilStart + it->duration, kWindowSeconds);
            statusSecs = secsUntilStart;
        }

        if (endSec <= startSec) continue;
        if (active && SubscriptionsBarHideActive) continue; // "only show what's not already happening" — see settings_table.h
        segs.push_back({
            "Basic:" + it->name, it->name, it->chatCode,
            secToX(startSec), secToX(endSec), active, statusSecs, endSec - startSec,
            BasicEventColorFor(it->name)
        });
    }

    for (const auto& key : g_SubscribedCyclicSlots)
    {
        auto it = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
            [&](const CyclicGroup& grp) { return grp.name == key.groupName; });
        if (it == g_CyclicGroups.end()) continue; // group deleted since subscribing

        for (const auto& slot : it->slots)
        {
            if (slot.offset != key.slotOffset) continue;

            int secondsOfDay = (int)(now % 86400);
            int repeat  = slot.repeat > 0 ? slot.repeat : 1;
            int subSpan = it->period / repeat;

            bool foundActive    = false;
            int  activeSecsLeft = 0;
            int  bestSecsUntil  = it->period;

            for (int r = 0; r < repeat; r++)
            {
                int baseOffset     = slot.offset + r * subSpan;
                int phase          = ((secondsOfDay - baseOffset) % it->period + it->period) % it->period;
                bool slotActive    = (phase < slot.duration);
                int secsUntilStart = slotActive ? 0 : (it->period - phase);

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
                if (bestSecsUntil >= kWindowSeconds) break; // not upcoming within the window
                active     = false;
                startSec   = bestSecsUntil;
                endSec     = std::min(bestSecsUntil + slot.duration, kWindowSeconds);
                statusSecs = bestSecsUntil;
            }

            if (endSec <= startSec) break;
            if (active && SubscriptionsBarHideActive) break; // "only show what's not already happening" — see settings_table.h; still break, same as the no-op-slot case above, since this key's one relevant occurrence has been resolved either way

            char offsetBuf[16];
            snprintf(offsetBuf, sizeof(offsetBuf), "%d", key.slotOffset);
            std::string label = it->name + " - " + slot.name;
            segs.push_back({
                "Cyclic:" + it->name + ":" + offsetBuf, label, slot.chatCode,
                secToX(startSec), secToX(endSec), active, statusSecs, endSec - startSec,
                it->SlotColor(slot)
            });
            break;
        }
    }

    std::sort(segs.begin(), segs.end(), [](const LineSegment& a, const LineSegment& b)
    {
        return a.startX < b.startX;
    });

    AssignLanes(segs);

    return segs;
}

// ---------------------------------------------------------------------------
// Smoothstep — Ken Perlin's improved version, matching the reference
// distribution-line.html's easing exactly (same formula, same purpose:
// soft, non-"edgy" transitions in/out of a dropped block).
// ---------------------------------------------------------------------------
static float SmoothStep(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ---------------------------------------------------------------------------
// FlatBlockDepthAt
// ---------------------------------------------------------------------------
// Depth profile (0..1) of a single "dropped" block at local-x position x,
// shaped to stay within [start, end] — the segment's OWN width — rather
// than flaring out past it: flat 0 outside [start, end] entirely, eases
// up across [start, start+tw], flat 1 across [start+tw, end-tw], eases
// back down across [end-tw, end]. So the widest point of the resulting
// shape (the flat top) is exactly the segment's width, with the curved
// shoulders tucked inward underneath it — not wider than the bar above
// it. (Earlier version eased across [start-tw, start] / [end, end+tw],
// which made the shoulders spill outside the segment's own x-range —
// the "wider at the top" look this replaces.) For segments narrower than
// 2*tw, tw is capped to half the segment's width so the two shoulders
// meet at the midpoint instead of overlapping/inverting.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Per-segment eased drop animation state, keyed by LineSegment::key.
// Mirrors displayDrops in distribution-line.html: each currently-hovered
// (or just-unhovered, mid-ease-out) segment eases its own depth toward
// a target every frame, independently of the others, so segments raise
// and lower smoothly rather than snapping.
//
// hoverSeconds tracks how long the mouse has continuously sat over this
// specific segment/dot (raw hover, before any delay is applied) — reset
// to 0 the instant hover is lost, so briefly passing over the strip
// (e.g. moving the mouse up to click a Nexus icon) doesn't "bank"
// partial progress toward a later, unrelated hover. The eased amount's
// target only becomes 1.0 once hoverSeconds clears
// SubscriptionsBarHoverDelayMs/1000 — see the easing loop below.
// ---------------------------------------------------------------------------
struct DropState { float amount = 0.0f; float hoverSeconds = 0.0f; };
static std::unordered_map<std::string, DropState> s_dropStates;

// ---------------------------------------------------------------------------
// PackStackRows
// ---------------------------------------------------------------------------
// Assigns each currently-hovered/dropping segment a row (0, 1, 2, ...) for
// the vertical stack, so segments whose x-ranges DON'T actually overlap
// each other can share a row instead of each claiming its own — e.g. one
// wide segment on row 0, plus two narrow segments that don't overlap one
// another sharing row 1, rather than spreading across three separate rows.
//
// Processing order is LONGEST-DURATION-FIRST (durationSecs descending,
// see LineSegment's field comment for why seconds and not live pixel
// width), soonest-first as a tiebreak for equal durations — NOT
// soonest-first alone. This is a deliberate user request: a
// longer-running bar should win the lowest available row over a
// shorter one it overlaps, even if the shorter one happened to already
// be resting in that row first (e.g. hovering a dot mid-way through an
// already-open 30-minute bar to reveal an overlapping 60-minute bar —
// the 60-minute bar should bump the 30-minute one up a row rather than
// itself being pushed into a pill). Only matters when segments actually
// x-overlap; two non-overlapping segments still freely share a row
// regardless of duration or order, same as before this changed (see the
// row-occupancy check below, which was widened from "does this start
// past the row's trailing edge" to "does this avoid EVERY existing
// occupant's span" specifically so a longer-but-later segment can still
// be correctly rejected from a row whose current occupant it would
// overlap in the middle of, not just at the tail).
//
// A segment simply takes the lowest-numbered row that has room for it,
// same greedy interval-packing approach as AssignLanes above, just reused
// at the hover-stack stage instead of the baseline-lane stage, with the
// longest-duration-first ordering layered on top.
//
// Returns, per segment key: {row index, that row's max eased depth}.
// rowMaxDepth is currently UNUSED by the row-to-Y conversion in
// stackTopY (see its own comment: row spacing must NOT scale by live
// eased depth, or a fully-open row overlaps a still-easing-in row below
// it — a real bug hit and fixed shortly after this was first written).
// Left in the returned struct since it's cheap to compute and may be
// useful for a future refinement (e.g. easing a row's OWN drawn shape),
// but don't assume something reads it just because it's here.
// ---------------------------------------------------------------------------
struct StackRowInfo
{
    int   row = 0;
    float rowMaxDepth = 0.0f;
};
static std::unordered_map<std::string, StackRowInfo> PackStackRows(
    const std::vector<LineSegment>& segs,
    const std::vector<int>& order, // indices into segs, soonest-first (re-sorted internally to longest-duration-first, see comment above)
    const std::unordered_map<std::string, DropState>& dropStates,
    const std::unordered_map<std::string, std::pair<float, float>>& dropBoundsByKey) // per-segment key -> edge-safe [dropX0, dropX1], see caller's comment — overlap is checked against THESE spans, not the segment's raw startX/endX, so widened (screen-edge) segments correctly get their own row against anything they now visually overlap
{
    constexpr float kRowMargin = 4.0f; // px — a little breathing room between two segments sharing a row, beyond bare x-touching
    std::unordered_map<std::string, StackRowInfo> result;

    // Re-sort processing order: longest-duration-first (durationSecs, a
    // stable integer, not live pixel width — see LineSegment's field
    // comment for why), soonest-first as the tiebreak (stable_sort
    // preserves `order`'s original soonest-first relative order for
    // equal durations). Row ASSIGNMENT (0, 1, 2...) still ends up
    // correlating with time for typically-similar-duration segments,
    // since the tiebreak is soonest-first — this only overrides that
    // when durations genuinely differ.
    std::vector<int> durationOrder = order;
    std::stable_sort(durationOrder.begin(), durationOrder.end(),
        [&](int a, int b)
        {
            return segs[a].durationSecs > segs[b].durationSecs;
        });

    // rowSpans[row] = every occupant's [startX, endX] placed in that row so
    // far. Was a single trailing rowEndX[row] float before this changed —
    // that assumed left-to-right insertion order within a row, which
    // soonest-first guaranteed but longest-duration-first no longer does
    // (a long segment starting LATER than a short one already in the row
    // must still be checked against the short one's full span, not just
    // whatever the row's rightmost edge happens to be at that point).
    std::vector<std::vector<std::pair<float, float>>> rowSpans;
    std::vector<float> rowDepth;  // rowDepth[row] = max eased depth among segments placed in that row so far

    for (int idx : durationOrder)
    {
        const LineSegment& s = segs[idx];
        auto dsIt = dropStates.find(s.key);
        float depth = (dsIt != dropStates.end()) ? dsIt->second.amount : 0.0f;

        // Use this segment's edge-safe widened bounds for overlap
        // purposes, not its raw startX/endX — see the caller's comment
        // on dropBoundsByKey for why (two segments that don't overlap in
        // their true narrow ranges can still overlap once widened for
        // screen-edge legibility, and row-packing needs to know that).
        // Falls back to the segment's own raw range if somehow missing
        // (shouldn't happen — dropBoundsByKey is built from the same
        // segs list — this is just defensive).
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

    // Second pass: now that every row's final max depth is known, write it
    // back into every segment's result entry (a row's height can only be
    // determined once ALL its occupants have been placed, which may
    // happen after an earlier occupant in soonest-first order).
    for (int idx : order)
    {
        const LineSegment& s = segs[idx];
        auto it = result.find(s.key);
        if (it != result.end()) it->second.rowMaxDepth = rowDepth[it->second.row];
    }

    return result;
}


static constexpr float kTransitionWidth = 26.0f; // px — curved shoulder width, scaled down from the HTML's 60px for a much thinner overlay strip
static constexpr float kEaseRate        = 0.18f; // per-frame lerp factor, matches the HTML's drop easing constant

// TEMPORARY (this session) — set true to dump every hovered segment's
// PackStackRows input/output to the Nexus log each frame something is
// hovered, to diagnose a reported bug: two adjacent, non-overlapping
// segments (Admiral Taidha 58m+15m, Great Jungle Wurm 73m+15m) landed on
// visibly different stack rows despite a standalone test of the exact
// same PackStackRows code, given the same startX/endX/order, correctly
// merging them into one row — so the bug (if real) must be in what
// actually reaches PackStackRows at runtime, not the packing algorithm
// itself. Off by default; flip to true, reproduce, paste the logged
// lines back for the next session, then flip back to false (or just
// remove this whole debug block, see addon.h's include comment above).
static constexpr bool kDebugLogStackPacking = true;

// ---------------------------------------------------------------------------
// Pill-detach phase thresholds (fractions of DropState::amount, 0..1)
// ---------------------------------------------------------------------------
// Mocked interactively with the user (visualize:show_widget) before this was
// written — see HANDOFF-subscriptions-bar.md's workflow note. Confirmed
// design: the block grows using the EXACT SAME FlatBlockDepthAt flat-
// top/curved-shoulder silhouette the bar already draws at rest (not a new
// "blob" shape), then its shoulders neck inward toward the middle (using the
// same depth function, inverted near the edges) until it visually separates
// from the baseline, then the freed shape eases from the flat-top/curved
// silhouette into a true stadium pill (rx = height/2) at a fixed vertical
// offset below the bar. The pill's WIDTH is locked to the segment's own
// on-bar width the entire time — only height/corner-radius/Y change during
// the neck-to-pill transition; the pill is never narrower than the segment
// was on the bar. Kept as named thresholds (not magic numbers inline) so the
// four phases stay easy to retune together without re-deriving the mock.
//   [0, kPinchStart)          : normal grow, full FlatBlockDepthAt silhouette, attached to baseline
//   [kPinchStart, kPinchEnd)  : shoulders neck inward, still attached, height/rx starting to ease toward pill
//   [kPinchEnd, kDetachEnd)   : detached pill shape, still easing width->locked/height->kPillH/rx->stadium, Y easing toward rest
//   [kDetachEnd, 1]           : fully resolved floating pill at rest offset, label shown
// ---------------------------------------------------------------------------
static constexpr float kPinchStart = 0.60f;
static constexpr float kPinchEnd   = 0.78f;
static constexpr float kDetachEnd  = 0.92f;

// ---------------------------------------------------------------------------
// EdgeSafeDropBounds
// ---------------------------------------------------------------------------
// Screen-edge segment handling (handoff "Future to-dos" #5, clarified by the
// user this session): an ACTIVE segment always starts at startX==0 (clamped
// to "now" — see LineSegment's comment), so one that's close to ending has a
// very narrow [0, endX] on-bar width. The dropped block/pill reuses that
// same x0..segEnd unchanged for its own width (see the pill-detach doc
// comment above — "pill width == segment's own on-bar width" was an
// explicit, confirmed design call), so a narrow-enough segment produces a
// pill too narrow to hold its own two-line label — the label overflowed the
// plate/pill. Same problem symmetrically possible at the right screen edge
// for an upcoming segment whose end got clamped to the window's right edge.
//
// User was shown two options (mocked interactively): (A) shrink the label
// text to fit the narrow pill, or (B) keep the pill's on-screen width at a
// legible minimum and let it slide its edge inward, off the segment's own
// true startX/endX, only for the DROPPED shape (never the resting baseline
// line, which always stays exactly at the segment's real x-range). User
// picked (B) — matches the existing "never shrink below a usable minimum"
// precedent already set for kMaxDropPx (see settings_table.h /
// SubscriptionsBarMaxDropPx's handoff history).
//
// minWidth was originally a flat guessed constant (kMinDropWidthPx, 90px).
// User reported that was still clipping/overly-generous depending on the
// actual label — asked for it to be derived from the real text box instead.
// Callers now measure each segment's own label (ImGui::CalcTextSize on
// both lines + the plate's own padding) and pass THAT in as minWidth, so
// the floor is exactly "however wide this segment's own label needs to
// be", not a one-size-fits-all guess. See the dropBoundsByKey precompute
// loop below for where that measurement happens.
//
// This only ever WIDENS the drop's x0/segEnd outward from the segment's own
// [startX, endX] — an already-wide segment is returned unchanged (the
// std::max/std::min below are no-ops once naturalW >= minWidth), so this
// cannot affect ordinary (non-edge) segments at all. The shift is clamped
// to stay inside [0, screenW] so the widened pill still can't poke past
// the actual screen edge on the far side either.
// ---------------------------------------------------------------------------
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
    // Prefer growing to the right first (reads more naturally left-to-right
    // with the timeline), then spill any remainder left; each side is
    // capped so the result never crosses the opposite screen edge.
    float growRight = std::min(deficit, std::max(0.0f, screenW - endX));
    float remaining = deficit - growRight;
    float growLeft  = std::min(remaining, std::max(0.0f, startX));

    outX0 = startX - growLeft;
    outX1 = endX + growRight;
}

// ---------------------------------------------------------------------------
// PillPinchFactor
// ---------------------------------------------------------------------------
// 0..1 "how much has this x-position necked inward", parameterized by
// neckT (0..1, how far through the neck-in sub-phase we are) — reuses
// FlatBlockDepthAt's own smoothstep shoulder logic but centered on the
// segment's own middle, so the neck-in reads as a continuation of the same
// curve language instead of a new interpolation curve.
//
// IMPORTANT continuity requirement: at neckT==1 this must return 0
// EVERYWHERE (not just at the center) — the shape must be back to a plain
// full-height rectangle right at the pinch/detach boundary, because that's
// exactly what the detached-pill branch starts drawing from (a square-
// cornered rect at pillRx==0). Pinch peaks in the MIDDLE of the neck-in
// sub-phase and relaxes back to 0 by its end, rather than monotonically
// increasing to a permanent taper — so the visible motion is "waist
// pinches in, then the whole silhouette squares back off into a rect
// right as it lets go of the baseline", not a taper that would otherwise
// pop straight into a rectangle at the phase boundary.
// ---------------------------------------------------------------------------
static float PillPinchFactor(float x, float start, float end, float neckT)
{
    if (neckT <= 0.0f || neckT >= 1.0f) return 0.0f; // relaxed at both ends of the sub-phase
    float half = (end - start) * 0.5f;
    if (half <= 0.0f) return 0.0f;
    float distFromEdge = std::min(x - start, end - x) / half; // 0 at either edge, 1 at center
    float waistShape = 1.0f - std::min(1.0f, distFromEdge / 0.55f); // strongest away from center, ~0 at true center
    // Envelope over neckT: 0 at neckT=0, peaks near neckT~0.6, back to 0 at neckT=1 (smoothstep up then down)
    float envelope = (neckT < 0.6f) ? SmoothStep(neckT / 0.6f) : SmoothStep((1.0f - neckT) / 0.4f);
    return std::max(0.0f, waistShape * envelope);
}

// ---------------------------------------------------------------------------
// PathRoundedRect
// ---------------------------------------------------------------------------
// Builds a rounded-rect path via direct PathArcTo calls, then fills/strokes
// it, instead of going through AddRectFilled/AddRect's own rounded-corner
// path. Same winding order/geometry AddRect itself builds (left side first:
// bottom-left arc -> top-left arc, then right side: top-right arc ->
// bottom-right arc; see ImDrawList::AddRect in imgui_draw.cpp) so this is a
// drop-in replacement, not a different shape.
//
// The difference from AddRect (this repo vendors ImGui 1.80, which has no
// ImDrawFlags_RoundCorners* — see the handoff's "Environment / build
// gotchas") is entirely in smoothness: AddRect's internal PathArcTo calls
// hardcode num_segments=3 per corner, which is faceted/visible at this
// pill's radius (see pill_stadium_vs_polygon_corners.svg — "Current:
// AddRect rounded corner" vs "Proposed: PathArcTo stadium"). Calling
// PathArcTo directly lets num_segments scale with the actual radius being
// drawn, so a small attached-block corner radius and a full stadium cap
// (rx = height/2) both read as smooth curves rather than a fixed facet
// count that's fine for tiny UI corners but visibly polygonal at pill
// scale.
//
// segments-per-quarter-circle scales with radius, floored so degenerate/
// zero radii (attached phase, pillRx==0) still produce a clean rect and
// never divide-by-zero or emit a zero-segment arc.
static void PathRoundedRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding)
{
    // ImClamp/IM_PI live in imgui_internal.h, which this file doesn't
    // otherwise include (see the includes list at the top) — use
    // std::min/max and a local pi constant instead of pulling in imgui's
    // internal header just for this.
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

    // Radius-scaled segment count per quarter-circle (4 segments per
    // quarter for a small corner, up to 16 at this bar's largest expected
    // pill radius) — smooth at stadium scale without over-tessellating
    // small attached-block corners every frame.
    int segsPerQuarter = std::max(4, std::min(16, (int)(rounding * 0.5f)));

    float x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    dl->PathArcTo(ImVec2(x0 + rounding, y1 - rounding), rounding, kPi * 0.5f, kPi,        segsPerQuarter); // bottom-left
    dl->PathArcTo(ImVec2(x0 + rounding, y0 + rounding), rounding, kPi,        kPi * 1.5f, segsPerQuarter); // top-left
    dl->PathArcTo(ImVec2(x1 - rounding, y0 + rounding), rounding, kPi * 1.5f, kPi * 2.0f, segsPerQuarter); // top-right
    dl->PathArcTo(ImVec2(x1 - rounding, y1 - rounding), rounding, 0.0f,       kPi * 0.5f, segsPerQuarter); // bottom-right
}

// ---------------------------------------------------------------------------
// RenderSubscriptionsBar
// ---------------------------------------------------------------------------
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
    if (screenW <= 0.0f) return;

    time_t now = time(nullptr);
    std::vector<LineSegment> segs = CollectVisibleSegments(now, screenW);
    if (segs.empty())
    {
        s_dropStates.clear();
        return;
    }
    std::vector<DotMark> dots = CollectOverlapDots(segs, screenW);

    // ---- Layout constants (local space: y=0 is the baseline strip itself,
    // dropped blocks extend DOWNWARD from there since the line lives on
    // the top edge) ----
    constexpr float kLineThick    = 2.0f;
    constexpr float kBaselineY    = kLineThick * 0.5f;  // line is centered on this y, so offsetting by half its thickness puts the stroke's visible top edge flush with the actual screen edge instead of half of it getting clipped off-screen
    // How far a single fully-hovered block drops down from the baseline
    // (and, once pill-detach kicks in, the pill's fixed height too — see
    // SubscriptionsBarMaxDropPx's comment in settings_table.h). User-
    // configurable; floored at 8px so the pill math further down (which
    // relies on a positive height for its stadium corner radius,
    // pillRx = kMaxDropPx/2) never degenerates from a 0-or-negative
    // setting value.
    const float kMaxDropPx = (float)std::max(8, SubscriptionsBarMaxDropPx);
    constexpr float kGapPx        = 3.0f;  // thin background-colored notch between adjacent lane-0 segments
    constexpr float kStackGapPx   = 4.0f;  // vertical gap between stacked dropped blocks when multiple segments are hovered at once
    constexpr float kDotRadius    = 2.5f;  // px
    constexpr float kDotSpacingPx = 7.0f;  // horizontal spacing between two dots that land on the exact same tick, so a cluster reads as "several dots" rather than one blob
    constexpr float kDotY         = kBaselineY + 8.0f; // dots sit a small, fixed distance below the baseline — not tied to any lane, since lanes no longer draw their own resting line
    constexpr float kDotHitRadius = 5.0f;  // generous click/hover target around each dot's visual radius
    // Label plate padding — hoisted here (was previously a local
    // constexpr right where the plate is drawn) so the edge-safe drop
    // bounds precompute below can size the minimum drop width off the
    // SAME padding the plate itself actually uses, rather than a
    // separately-guessed constant that could drift out of sync with it.
    constexpr float kLabelPadX    = 6.0f;
    constexpr float kLabelPadY    = 3.0f;

    // Nudge co-occurring dots (multiple hidden events starting on the
    // exact same tick) apart horizontally so they render as a small
    // cluster of distinct dots rather than one indistinguishable blob —
    // matches "4 events start here -> 4 dots side by side" from the
    // reference mock. Computed once per frame since dots.size() is tiny
    // (a handful of subscriptions at most).
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

    // Which segments are currently under the mouse. Two ways in:
    //  1. The mouse is over a lane-0 segment's own x-range, anywhere from
    //     the baseline down through the full vertical extent that segment
    //     could possibly be dropped/detached to (a "hover column", not a
    //     thin band). A thin band-only test only covers the resting line
    //     itself: once a block pops out and the user moves the mouse
    //     straight down onto it, they cross empty vertical space with no
    //     hover coverage on the way, hover is lost mid-transit, the block
    //     eases back up, and the mouse arrives where the block used to be
    //     — this was the "can't reach the pill" / "click does nothing"
    //     bug. Originally fixed with a per-segment worst-case column (down
    //     to the deepest ANY pill could ever rest) but that was too tall
    //     for a plain safe-zone pop-out or a barely-detached pill — exiting
    //     felt like it required crossing much more empty space than the
    //     block/pill's own actual height. Replaced with a single SHARED
    //     horizontal band across the full screen width, whose bottom edge
    //     tracks the deepest thing actually open THIS moment — computed
    //     just below from every segment currently mid-drop (amount > 0),
    //     using last frame's depth/stacking/unsafe-zone state (one frame
    //     of latency, same reasoning as everywhere else in this file: the
    //     eased values barely move frame to frame, so this is visually
    //     exact). Being a single shared band rather than one column per
    //     segment also means it naturally covers dot-hover for free (see
    //     point 2 below) and grows/shrinks correctly as segments
    //     stack/unstack, without extra bookkeeping.
    //  2. The mouse is over one of that segment's dot markers (a lane>0
    //     segment has no line of its own to hover, only its dot) — hit-
    //     tested as a small circle around the dot's nudged draw position,
    //     for STARTING a hover (the dot itself is tiny and needs precise
    //     targeting to trigger). Once a dot's segment has actually started
    //     dropping, the shared sustain band below covers it same as any
    //     other segment, so leaving the tiny dot circle but staying within
    //     the open band no longer instantly collapses it.
    // All feed into the same hoveredIndices list, so a hidden segment
    // dropping in via its dot stacks together with the shown segment
    // exactly like two lane-0 segments would.
    constexpr float kLineHalfHeight = (kLineThick + 1.0f) * 0.5f;
    constexpr float kHoverBand = kLineHalfHeight + 2.0f; // the ONLY band that can start a fresh pop-out
    // Shared sustain-band bottom: baseline by default (nothing open yet),
    // pushed down to cover whichever currently-mid-drop segment(s) reach
    // deepest, mirroring the same running-stack math used for the actual
    // draw (stackTopY below) and the same topY/pillY unsafe-zone-detach
    // math used per-segment in the draw loop — but using LAST frame's
    // amount/order, since this frame's stack isn't known yet at this
    // point (computed from hoveredIndices, which we're still building).
    float sustainBottom = kBaselineY + kHoverBand;
    {
        // Reuse the exact same "soonest first" stacking order the real
        // draw uses, over every segment that was mid-drop last frame —
        // not just this frame's not-yet-known hoveredIndices — so a
        // segment that's still easing OUT (no longer hovered, but not
        // back to 0 yet) still gets covered until it's actually gone.
        std::vector<int> openLastFrame;
        for (int i = 0; i < (int)segs.size(); i++)
            if (s_dropStates[segs[i].key].amount > 0.001f) openLastFrame.push_back(i);
        std::sort(openLastFrame.begin(), openLastFrame.end(), [&](int a, int b)
        {
            return segs[a].statusSecs < segs[b].statusSecs;
        });

        // This estimate intentionally does NOT use the row-packing from
        // PackStackRows below (see to-do #7/#8 stacking rework) — it only
        // needs a conservative UPPER BOUND on how deep the sustain band
        // must reach, and one-row-per-segment can only ever be taller
        // than (or equal to) the real packed stack, never shorter. Using
        // the simpler unpacked math here keeps this estimate safely
        // conservative without needing to duplicate the packing logic a
        // frame early, before this frame's real hoveredIndices exists.
        float runningY = kBaselineY;
        float runningPillY = (float)std::max(0, SubscriptionsBarUnsafeHeightPx); // mirrors pillStackY's own base, computed properly further down for the actual draw
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
                runningPillY += kMaxDropPx + kStackGapPx; // reserve this pill's own slot for whatever's stacked after it
            }
            float blockBottom = std::max(topY, pillY) + kMaxDropPx;

            sustainBottom = std::max(sustainBottom, blockBottom + 4.0f); // small slop, not a full worst-case column

            runningY += kMaxDropPx * depth + kStackGapPx * depth;
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
        // Sustain path: only for segments already mid-drop (amount > 0),
        // test against the SHARED band computed above (baseline down to
        // sustainBottom) over that segment's own x-range PADDED by a
        // small fixed margin left/right (kSustainPadX) — not just its
        // bare width. Two reasons this needs padding, unlike the trigger
        // test above:
        //   1. A dot-triggered (lane>0) segment can have a very narrow
        //      timeline extent, and the mouse arrives at its dot's tiny
        //      hit-circle, not necessarily dead-center over that narrow
        //      range — a bare-width sustain zone was too tight, so
        //      leaving the dot's circle immediately dropped the pop-out
        //      again even while still visually "over" it.
        //   2. Lane>0 segments were ALSO being skipped here entirely
        //      (stale `if (lane != 0) continue`, leftover from the
        //      lane-0-only trigger test above) — dots have no line of
        //      their own to trigger a hover, but once open via their dot,
        //      they still need sustain coverage like anything else. Fixed
        //      by removing that check for this path specifically.
        // A fresh/idle segment (amount == 0) never reaches this — it can
        // only ever be triggered by the thin band (lane-0) or a dot's own
        // small circle (lane>0) above.
        if (mouse.y >= kBaselineY - kHoverBand && mouse.y <= sustainBottom)
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
    // Stacking order: soonest-starting (or currently active) segment on
    // top, since that's usually the more time-critical one to read first.
    // MUST be std::stable_sort, not std::sort: two hovered segments can
    // share the exact same statusSecs (e.g. two upcoming events starting
    // in the same number of seconds), and an unstable sort's tie-break
    // is unspecified — it can silently flip which of the two comes first
    // from one frame to the next even though neither segment's own data
    // changed. That reordering fed directly into PackStackRows as its
    // `order` param, which uses this same order as ITS tiebreak whenever
    // two segments have equal durationSecs — so an unstable tie here was
    // the real source of a reported "stack rows keep switching every
    // second or two" bug that survived switching PackStackRows from live
    // pixel width to stable integer duration; the duration value itself
    // was never the flickering part, the incoming order was. The
    // std::string key comparison as a final tiebreak (below the
    // statusSecs compare) makes the order fully deterministic even for
    // an exact statusSecs tie, rather than leaving it to whatever
    // relative order segs happened to be in.
    std::stable_sort(hoveredIndices.begin(), hoveredIndices.end(), [&](int a, int b)
    {
        if (segs[a].statusSecs != segs[b].statusSecs) return segs[a].statusSecs < segs[b].statusSecs;
        return segs[a].key < segs[b].key;
    });

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ---- Track raw hover duration, then ease every segment's drop
    // amount toward a target gated by the configured delay ----
    float dt = io.DeltaTime > 0.0f ? std::min(io.DeltaTime, 0.1f) : 0.0f;
    // Frame-rate-independent version of the HTML's fixed *=0.12 lerp-per-
    // frame: raise the per-frame rate to a power of (dt * 60) so the same
    // half-life holds regardless of the addon's actual frame rate.
    float easeThisFrame = 1.0f - powf(1.0f - kEaseRate, dt > 0.0f ? dt * 60.0f : 1.0f);
    float hoverDelaySeconds = std::max(0, SubscriptionsBarHoverDelayMs) / 1000.0f;

    for (int i = 0; i < (int)segs.size(); i++)
    {
        bool isHovered = std::find(hoveredIndices.begin(), hoveredIndices.end(), i) != hoveredIndices.end();
        DropState& st = s_dropStates[segs[i].key];

        // Raw hover duration resets instantly on losing hover (no
        // "banking" partial progress across separate hovers), and only
        // accumulates while actually hovered this frame.
        st.hoverSeconds = isHovered ? (st.hoverSeconds + dt) : 0.0f;

        // The drop only targets 1.0 once the raw hover has cleared the
        // configured delay — SubscriptionsBarHoverDelayMs=0 means
        // hoverDelaySeconds is 0, so st.hoverSeconds >= 0 is satisfied
        // on the very first hovered frame, i.e. instant, same as before
        // this feature existed. Losing hover always targets 0
        // immediately (no equivalent delay on the way back down — a
        // delayed pop-in reads as responsive, a delayed pop-OUT reads as
        // laggy/stuck).
        bool pastDelay = isHovered && st.hoverSeconds >= hoverDelaySeconds;
        float target = pastDelay ? 1.0f : 0.0f;

        st.amount += (target - st.amount) * easeThisFrame;
        if (fabsf(st.amount - target) < 0.001f) st.amount = target;
    }

    // Drop stale keys (segment no longer visible this frame, e.g. it
    // ended or scrolled out of the 2h window) so the map doesn't grow
    // forever across a long play session.
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

    // ---- Baseline: thin line across the full screen width — matches the
    // HTML's shared baseline path. ----
    dl->AddLine(ImVec2(0, kBaselineY), ImVec2(screenW, kBaselineY),
        IM_COL32(255, 255, 255, 90), kLineThick);

    // ---- Overlap dot markers: one plain white dot per hidden (lane>0)
    // event that starts on a 5-minute tick overlapping whatever's shown
    // there. Colorless on purpose — with dozens of subscriptions a color
    // key isn't something a user can realistically memorize, so a dot
    // only signals "something's here", and hovering (below) is what
    // reveals which event and its actual color, same as hovering any
    // shown segment already does. Dots for segments currently mid-drop
    // fade out as their block rises, so the dot doesn't visually clash
    // with the now-visible dropped block.
    for (size_t d = 0; d < dots.size(); d++)
    {
        float depth = s_dropStates[segs[dots[d].segIndex].key].amount;
        float alpha = 1.0f - depth; // fades out as this dot's own segment drops in
        if (alpha <= 0.02f) continue;
        dl->AddCircleFilled(ImVec2(dotDrawX[d], kDotY), kDotRadius, IM_COL32(255, 255, 255, (int)(235 * alpha)), 12);
    }

    // Each stacked drop's own top-of-block y is computed as this frame's
    // running total of the depths/heights of whichever hovered segments
    // are stacked above it, so drops stack snugly without gaps and
    // without overlapping each other, and shrink back together as their
    // shared hover ends. Filled in below as each hovered segment is drawn.
    //
    // Unlike the earlier version of this feature, the running total's
    // STARTING point is now ALWAYS the baseline (kBaselineY), regardless
    // of unsafe-zone status — an attached pop-out always grows straight
    // down from the bar itself, drawing over GW2's own corner UI same as
    // any other segment, exactly like the user asked for. Unsafe-zone
    // avoidance now happens only once a segment has fully DETACHED into a
    // pill (see the per-segment draw loop's pillY: it eases toward
    // SubscriptionsBarUnsafeHeightPx as its own resting Y, but only for
    // segments whose own zone check is true) — so the block pops out normally first, then peels
    // away and clears the corner UI as it becomes a pill, rather than
    // starting pre-offset down.
    std::unordered_map<std::string, float> stackTopY;

    // ---- Edge-safe drop bounds, precomputed once per segment ----
    // Screen-edge segment handling: compute each segment's DROPPED
    // block/pill x-range up front (same [x0, segEnd] gap treatment the
    // draw loop below uses, then widened via EdgeSafeDropBounds if
    // needed) and key it by segment key, so every downstream consumer —
    // row-packing, drawing, click hit-testing — agrees on the exact same
    // widened bounds.
    //
    // This used to be computed independently, inline, in the draw loop
    // and the click hit-test loop, with PackStackRows in between still
    // reading raw seg.startX/endX. That mismatch was a real bug: two
    // segments whose TRUE narrow ranges didn't overlap (so row-packing
    // correctly gave them both row 0) could still end up visually
    // overlapping once independently widened at draw time — row-packing
    // never knew about the widening, so no stack offset was ever applied
    // and the pills rendered directly on top of each other. Precomputing
    // bounds before PackStackRows runs, and having it pack using THESE
    // spans instead of the raw ones, is what fixes that: any two
    // segments whose widened boxes actually overlap on screen now
    // correctly land in different rows.
    std::unordered_map<std::string, std::pair<float, float>> dropBoundsByKey;
    dropBoundsByKey.reserve(segs.size());
    for (const auto& seg : segs)
    {
        float bx0 = seg.startX;
        float bx1 = seg.endX;
        if (seg.endX < screenW) bx1 -= kGapPx;
        if (bx1 <= bx0) bx1 = bx0 + 1.0f;

        // Minimum drop width derived from the actual label this segment
        // will draw (see EdgeSafeDropBounds' comment — this replaced a
        // flat guessed constant per user feedback). Uses the same
        // SegmentStatusLine() the draw loop's label block calls, plus
        // that same block's plate padding (kLabelPadX, hoisted above so
        // both sides read the identical value), so this measurement can
        // never silently drift from what's actually drawn.
        ImVec2 nameSize   = ImGui::CalcTextSize(seg.name.c_str());
        ImVec2 statusSize = ImGui::CalcTextSize(SegmentStatusLine(seg).c_str());
        float minWidth = std::max(nameSize.x, statusSize.x) + kLabelPadX * 2.0f;

        float dropX0, dropX1;
        EdgeSafeDropBounds(bx0, bx1, screenW, minWidth, dropX0, dropX1);
        dropBoundsByKey[seg.key] = { dropX0, dropX1 };
    }

    std::unordered_map<std::string, StackRowInfo> stackRows = PackStackRows(segs, hoveredIndices, s_dropStates, dropBoundsByKey);

    // TEMPORARY (this session) — see kDebugLogStackPacking's comment
    // above. Logs once per frame while ANY segment is hovered, so expect
    // a burst of near-identical lines while the mouse sits still — that's
    // fine, just grab any one frame's worth once it looks wrong on
    // screen. Each line is one hoveredIndices entry, in the exact order
    // PackStackRows received them (soonest-first), showing exactly what
    // the packing decision was based on.
    if (kDebugLogStackPacking && !hoveredIndices.empty() && APIDefs)
    {
        char buf[256];
        APIDefs->Log(LOGL_INFO, "WorldEvents", "---- stack pack frame ----");
        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            float depth = s_dropStates[s.key].amount;
            int row = -1;
            auto it = stackRows.find(s.key);
            if (it != stackRows.end()) row = it->second.row;
            snprintf(buf, sizeof(buf),
                "key=%s startX=%.1f endX=%.1f statusSecs=%d depth=%.2f -> row=%d",
                s.key.c_str(), s.startX, s.endX, s.statusSecs, depth, row);
            APIDefs->Log(LOGL_INFO, "WorldEvents", buf);
        }
    }

    {
        // Convert each segment's packed row index into a cumulative Y:
        // walk rows in ascending order, each row's height is that row's
        // own max eased depth (the deepest occupant sharing it), so a row
        // holding only shallow/still-easing-in segments doesn't reserve
        // full kMaxDropPx worth of space it isn't using yet.
        int maxRow = -1;
        for (auto& kv : stackRows) maxRow = std::max(maxRow, kv.second.row);

        // Row spacing is NOT scaled by live eased depth here, unlike a
        // single segment's own animated Y. Reason: row 0 can already be
        // fully open (depth==1) while row 1 is still easing in fresh
        // (depth<<1) — if row 1's reserved space were scaled by ITS OWN
        // still-small depth, row 1's rowTopY would sit too close to row
        // 0's ALREADY-FULL-HEIGHT block, overlapping it (row 0 doesn't
        // shrink just because row 1 hasn't grown yet). Reserve full
        // height for every occupied row unconditionally instead — the
        // per-segment draw loop still animates each segment's own drawn
        // shape growing from 0, this only affects how much space
        // subsequent rows are pushed down by, which must already assume
        // "this row could be full height" from the moment it exists.
        std::vector<float> rowTopY(maxRow + 1, 0.0f);
        float runningY = kBaselineY;
        for (int row = 0; row <= maxRow; row++)
        {
            rowTopY[row] = runningY;
            runningY += kMaxDropPx + kStackGapPx;
        }

        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto it = stackRows.find(s.key);
            if (it != stackRows.end()) stackTopY[s.key] = rowTopY[it->second.row];
        }
    }

    // Detached pills all ease toward the SAME configured clearance
    // (SubscriptionsBarUnsafeHeightPx) once fully detached — that's a
    // fixed absolute Y, not derived from stackTopY, since the whole point
    // of detaching is to leave the attached stack behind and clear the
    // corner UI. But with two+ unsafe-zone segments open at once (e.g. a
    // dot's pill opened while another pill is already resting), both
    // converging on that same single absolute Y stacks them exactly on
    // top of each other right at the moment they finish detaching, even
    // though their ATTACHED phase (stackTopY above) was correctly
    // staggered. pillStackY gives each detached/detaching segment its own
    // slot below the configured clearance, same soonest-first order and
    // same kStackGapPx spacing as the attached stack, so pills stack
    // instead of overlapping. This is one shared pass/slot pool for every
    // pill regardless of WHY it detached (unsafe-zone or stack-position,
    // see the inUnsafeZone/stackDetach check just below) so a pill from
    // either trigger can never land on the same Y as another.
    std::unordered_map<std::string, float> pillStackY;
    {
        // Base clearance is the LARGER of the configured unsafe-zone
        // clearance and "past every attached (non-pill) row that sits
        // above the first pill row." A pill's own row index only tells
        // PackStackRows about OTHER pills sharing pillStackY's slot
        // pool — it says nothing about a plain attached block sitting
        // in row 0 while row 1 detaches into a pill (e.g. a wide
        // safe-zone block with a narrower stack-detached segment
        // beneath it). Without this, pillStackY's first slot started
        // flat at SubscriptionsBarUnsafeHeightPx regardless of whether
        // row 0 was itself a pill or just an ordinary attached block
        // occupying that same screen space — so a small
        // SubscriptionsBarUnsafeHeightPx (e.g. 30px, well inside a
        // kMaxDropPx=54 attached block's own height) let a row-1 pill
        // land its resting Y INSIDE row 0's attached block. Attached
        // rows only exist in stackTopY, keyed by row via the same
        // row-ascending stackTopY conversion above, so walk every row
        // from 0 up to (but excluding) the lowest row that has any
        // pill this frame, and push the base past each one's bottom
        // edge (topY + kMaxDropPx + kStackGapPx) — mirroring the exact
        // spacing stackTopY itself already reserves per row.
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

        float unsafeRestY = (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
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
                    runningPillY = std::max(runningPillY, rowTopIt->second + kMaxDropPx + kStackGapPx);
            }
        }

        // Keyed by row, not by segment: two segments that PackStackRows
        // already packed into the same row are, by construction,
        // non-overlapping in x — so they can safely share one pill Y
        // slot instead of each claiming its own. Without this, a pair
        // that sits side-by-side while attached (same row) visibly
        // separates vertically the moment both detach into pills, since
        // the old per-segment loop below incremented runningPillY once
        // per segment regardless of row. Segments with no row (not in
        // stackRows, e.g. a lone unsafe-zone dot with nothing else
        // hovered) fall back to a synthetic per-segment "row" via their
        // own key, so they still each get a distinct slot as before.
        // Collect (row, key) pairs first rather than assigning slots
        // while walking hoveredIndices directly — hoveredIndices is
        // soonest-first, NOT row-ascending, so a row-1 segment can be
        // encountered before row-0's own pill (e.g. row 0 sits in the
        // unsafe zone and is also a pill). Handing out runningPillY in
        // encounter order let row 1 grab the topmost slot ahead of row
        // 0's pill, so they'd render at the same Y / row 1 wouldn't
        // reliably land below row 0. Sorting by row first (stable, so
        // soonest-first is preserved within a row) guarantees slots are
        // always handed out row 0, then row 1, then row 2, etc. —
        // matching stackTopY's own ascending-row order — so a lower row
        // can never end up above a higher one just because it happened
        // to be hovered/processed first.
        struct PillCandidate { int row; std::string key; };
        std::vector<PillCandidate> candidates;
        int nextSyntheticRow = -1000000; // negative space, well clear of any real row index

        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            auto rowIt = stackRows.find(s.key);
            bool stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0); // to-do #8: any non-top row detaches into a pill, regardless of unsafe-zone
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(s, screenW);
            if (!inUnsafeZone && !stackDetach) continue; // only pills need a slot here

            float depth = s_dropStates[s.key].amount;
            float detachT = std::min(1.0f, std::max(0.0f, (depth - kPinchEnd) / (kDetachEnd - kPinchEnd)));
            if (detachT <= 0.0f) continue; // not detaching yet — still using stackTopY, no pill slot needed

            int row = (rowIt != stackRows.end()) ? rowIt->second.row : nextSyntheticRow--;
            candidates.push_back({row, s.key});
        }

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
                runningPillY += kMaxDropPx + kStackGapPx; // pills are always full height once they have any slot at all
            }
            else
            {
                pillStackY[c.key] = slotIt->second; // same row already has a slot — share it
            }
        }
    }

    // ---- Per-segment colored baseline overlay (lane-0 only) + dropped
    // block (any segment currently easing toward/away from a hover,
    // lane-0 or not). ----
    for (int i = 0; i < (int)segs.size(); i++)
    {
        const LineSegment& seg = segs[i];
        float depth = s_dropStates[seg.key].amount;

        // Baseline line ALWAYS uses the segment's true x-range — only the
        // dropped block/pill below gets the edge-safe minimum-width
        // treatment (see EdgeSafeDropBounds). Keeping the resting line
        // exact-to-time is what lets the dots/lane math above continue to
        // reason about real x-positions untouched.
        float x0 = seg.startX;
        float x1 = seg.endX;
        // Small gap before this segment's right edge so adjacent segments
        // read as distinct blocks, same "gap rectangle" idea as the HTML
        // reference — implemented here as simply not drawing quite to x1.
        float segEnd = x1;
        if (seg.endX < screenW) segEnd -= kGapPx;
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        ImU32 segColor = seg.color;
        ImU32 fillColor = (segColor & 0x00FFFFFF) | ((ImU32)(255 * (0.25f + 0.75f * (seg.active ? 1.0f : 0.7f))) << 24);

        if (seg.lane == 0)
        {
            // Colored baseline segment (always visible, even at depth 0)
            // — this is what makes the strip read as "N colored ticks"
            // at a glance before the user ever hovers anything. Only
            // lane-0 segments get this: exactly one line's worth of
            // height at rest, regardless of how many events overlap —
            // overlap is signaled by dots instead (drawn above), not by
            // a second permanent line.
            dl->AddLine(ImVec2(x0, kBaselineY), ImVec2(segEnd, kBaselineY), segColor, kLineThick + 1.0f);
        }

        if (depth > 0.002f)
        {
            // Stacked target y for this segment's dropped block: if it's
            // one of the currently-hovered segments, this is its
            // reserved slot in the shared stack (topmost = soonest);
            // otherwise (mid-ease-out, no longer hovered but still
            // animating back down) fall back to the baseline itself —
            // lane>0 segments have no resting y of their own to ease
            // from, so they drop from/return to the same baseline origin
            // every lane-0 segment at that x would use. Unsafe-zone
            // avoidance is handled later, only for the detached-pill Y,
            // not here.
            float topY = kBaselineY;
            auto stackIt = stackTopY.find(seg.key);
            if (stackIt != stackTopY.end()) topY = stackIt->second;

            // Edge-safe drop bounds (screen-edge segment handling):
            // looked up from dropBoundsByKey, precomputed once above
            // (before PackStackRows ran) so row-packing and drawing
            // agree on the exact same widened bounds — see that
            // precompute block's comment for why recomputing this
            // independently here was a bug (row-packing used to pack
            // against the raw narrow bounds while drawing widened
            // independently, so widened pills could silently overlap).
            // Only the drop shape below uses these — the baseline line
            // above already drew at the real x0/segEnd and is
            // unaffected. Falls back to the raw x0/segEnd if somehow
            // missing (shouldn't happen).
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

            // Pill-detach applies to a segment for either of two reasons:
            // (1) its x-range overlaps an unsafe zone (GW2's own corner
            // UI) — the original trigger — or (2) it's not in row 0 of
            // the current hover stack, i.e. some other segment is stacked
            // above it (to-do #8: "detached from the bar" is defined as
            // stack position — any non-top row — the simplest of the
            // three candidate definitions discussed, and the one that
            // reuses the existing pinch/detach machinery outright rather
            // than adding a new geometric condition). A segment with
            // neither is a plain attached pop-out exactly like before
            // this feature existed — pinchT/detachT permanently pinned to
            // 0, so the "if (depth < kPinchEnd)" branch below is the ONLY
            // branch it ever takes, at full FlatBlockDepthAt depth with
            // no pinch — identical output to the original single-shape
            // implementation.
            bool inUnsafeZone = SegmentOverlapsUnsafeZone(seg, screenW);
            bool stackDetach = false;
            {
                auto rowIt = stackRows.find(seg.key);
                stackDetach = (rowIt != stackRows.end() && rowIt->second.row > 0);
            }
            bool shouldDetach = inUnsafeZone || stackDetach;

            // Phase split, per the confirmed mock (see the kPinchStart/
            // kPinchEnd/kDetachEnd block comment above): grow -> neck-in
            // (still attached, still FlatBlockDepthAt-shaped) -> detach
            // into a locked-width stadium pill -> settle at rest. Only
            // reached at all for segments that shouldDetach.
            float pinchT  = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchStart) / (kPinchEnd - kPinchStart))) : 0.0f;
            float detachT = shouldDetach ? std::min(1.0f, std::max(0.0f, (depth - kPinchEnd)   / (kDetachEnd - kPinchEnd))) : 0.0f;

            // Corner radius eases from 0 (attached, square-ish flat-top
            // block) to a true stadium cap (rx = kMaxDropPx/2) as
            // detachT completes. Height stays fixed at kMaxDropPx the
            // whole time — same height as a normal safe-zone pop-out,
            // per the user's call — so the pill never gets cramped for
            // its two lines of label text; only Y position and corner
            // rounding change once it detaches, not height.
            float blockH = kMaxDropPx;
            float pillRx = (blockH * 0.5f) * detachT; // 0 while still attached/necking, ramps to a true stadium (rx=h/2) only once detaching

            // Pill Y once detached: eases from topY (where it popped out,
            // over the corner UI, same as any block) DOWN to this
            // segment's own reserved slot in pillStackY — NOT a single
            // shared SubscriptionsBarUnsafeHeightPx for every pill, which
            // made two+ simultaneously-open unsafe-zone pills converge on
            // the exact same absolute Y and sit stacked directly on top of
            // each other the moment they finished detaching (e.g. hovering
            // a dot while another pill was already resting). pillStackY
            // gives each one its own staggered slot below the configured
            // clearance instead, same soonest-first order as the attached
            // stack. Falls back to the flat clearance value if this
            // segment somehow has no slot yet (shouldn't normally happen
            // once detachT > 0, since pillStackY is built from the same
            // hoveredIndices set — this is just defensive).
            float unsafeRestY = (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
            auto pillIt = pillStackY.find(seg.key);
            if (pillIt != pillStackY.end()) unsafeRestY = pillIt->second;
            float pillY = topY + (unsafeRestY - topY) * detachT;

            // TEMPORARY (this session) — see kDebugLogStackPacking's
            // comment near kTransitionWidth. Extends the existing
            // per-frame stack-pack log with the actual draw-time values
            // this loop resolves to, since the earlier log only showed
            // PackStackRows' row assignment, not what topY/pillY/detachT
            // end up being once the pill-detach math runs. Same
            // once-per-frame-while-hovered gating.
            if (kDebugLogStackPacking && APIDefs)
            {
                char buf[256];
                auto rowItDbg = stackRows.find(seg.key);
                int rowDbg = (rowItDbg != stackRows.end()) ? rowItDbg->second.row : -1;
                snprintf(buf, sizeof(buf),
                    "  draw key=%s row=%d topY=%.1f unsafeRestY=%.1f detachT=%.2f pillY=%.1f shouldDetach=%d inUnsafeZone=%d stackDetach=%d",
                    seg.key.c_str(), rowDbg,
                    topY, unsafeRestY, detachT, pillY, (int)shouldDetach, (int)inUnsafeZone, (int)stackDetach);
                APIDefs->Log(LOGL_INFO, "WorldEvents", buf);
            }

            if (depth < kPinchEnd || !shouldDetach)
            {
                // ---- Phases 1-2 (or the ONLY phase for safe-zone
                // segments): attached, FlatBlockDepthAt silhouette,
                // shoulders necking inward via PillPinchFactor as depth
                // crosses kPinchStart (unsafe-zone segments only —
                // pinchT is 0 for safe-zone ones, so PillPinchFactor is a
                // no-op and this reduces to the original single-shape
                // drop exactly). Height stays fixed at kMaxDropPx
                // throughout. ----
                // Uses dropX0/dropX1 (edge-safe, may be wider than the
                // segment's true x0/segEnd — see EdgeSafeDropBounds) for
                // the whole silhouette, not just cx/segW, so the shape
                // that's drawn matches the width those were computed from.
                float tw = kTransitionWidth;
                int samples = std::max(8, (int)(segW / 4.0f));

                dl->PathClear();
                dl->PathLineTo(ImVec2(dropX0, topY));
                for (int s = 0; s <= samples; s++)
                {
                    float x = dropX0 + segW * (s / (float)samples);
                    float d = FlatBlockDepthAt(x, dropX0, dropX1, tw) * depth;
                    float pinch = PillPinchFactor(x, dropX0, dropX1, pinchT);
                    d *= (1.0f - pinch);
                    float y = topY + d * blockH;
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
                    float y = topY + d * blockH;
                    dl->PathLineTo(ImVec2(x, y));
                }
                dl->PathStroke(segColor, false, kLineThick);
            }
            else
            {
                // ---- Phases 3-4: detached. A true rounded rect whose
                // width is locked to the segment's own on-bar width the
                // whole time (never narrower than the bar above it, per
                // the confirmed mock) and whose corner radius eases up to
                // a full stadium cap (rx = h/2) as detachT completes.
                // Uses dropX0/dropX1, not the raw x0/segEnd, so an
                // edge-widened segment's pill stays widened once detached
                // too (see EdgeSafeDropBounds).
                //
                // Drawn via PathRoundedRect (direct PathArcTo, radius-
                // scaled segment count) instead of AddRectFilled/AddRect's
                // own fixed-3-segment-per-corner rounding — at a full
                // stadium radius (rx = h/2) that fixed facet count is
                // visibly polygonal (see pill_stadium_vs_polygon_corners.
                // svg). Same shape/geometry, smoother corners. ----
                ImVec2 pillP0(dropX0, pillY);
                ImVec2 pillP1(dropX1, pillY + blockH);

                dl->PathClear();
                PathRoundedRect(dl, pillP0, pillP1, pillRx);
                dl->PathFillConvex(fillColor);

                dl->PathClear();
                PathRoundedRect(dl, pillP0, pillP1, pillRx);
                dl->PathStroke(segColor, true, kLineThick);
            }

            // Label + status, vertically centered inside this block/pill's
            // own slice of the stack, fading in with depth so it doesn't
            // pop in abruptly.
            if (depth > 0.35f)
            {
                std::string line1 = seg.name;
                std::string line2 = SegmentStatusLine(seg);
                ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
                ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());

                // Vertically center both lines inside this block/pill's own
                // current height and Y (blockH/pillY, both already eased
                // above) rather than placing them below it — matches the
                // HTML reference, where the label lives inside the filled
                // shape, not underneath it. Uses the drop's steady-state
                // depth for the layout math so text doesn't visibly slide
                // as depth eases toward 1.0 — it fades in in place instead.
                float blockTop    = (depth < kPinchEnd || !shouldDetach) ? topY : pillY;
                float blockBottom = blockTop + blockH;
                float textBlockH  = size1.y + size2.y;
                float labelY      = blockTop + (blockBottom - blockTop - textBlockH) * 0.5f;

                float alpha = (depth - 0.35f) / 0.65f;
                ImU32 textCol = IM_COL32(255, 255, 255, (int)(230 * alpha));

                // Tight gray backing plate behind the label instead of a
                // text outline — tried a 4-direction outline first (see
                // git history / handoff), user reported it didn't look
                // good, asked for a plate instead. A dark, slightly
                // translucent gray rect sized to the two lines' combined
                // bounding box (plus a small pad) sits behind both lines,
                // so legibility no longer depends on what's behind the
                // segment's own fill color — reads cleanly against any
                // game background without the "haze" look the outline had.
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

    // Click on a hovered/dropped segment copies its waypoint code, same
    // affordance as a row-click in the text watchlist window. Only
    // segments that have actually finished (or nearly finished) dropping
    // are eligible — clicking during the hover-delay window, before
    // there's any visible block to click on, shouldn't silently copy
    // something the user can't see yet.
    //
    // IMPORTANT: this bar is drawn entirely on the background draw list,
    // with no ImGui::Begin() window anywhere (deliberate — see the
    // handoff). That means a plain ImGui::IsMouseClicked() check here
    // does NOT reliably fire: Nexus's WndProc hook only forwards mouse
    // clicks into ImGui's input queue for screen positions actually
    // covered by a live, non-NoInputs ImGui window/item that frame — a
    // raw background-drawlist shape has no such item, so a "cold" click
    // that only ever hovers empty background space above the game world
    // never reaches ImGui as a click at all (confirmed: clicking some
    // other real ImGui element first, so ImGui already has that frame's
    // click, made this fire — proving the hit-test math itself was
    // already correct). RenderMapEvents/RenderCyclicGroups hit this same
    // constraint for their drag-to-reposition anchors and solve it the
    // same way: a small invisible window + InvisibleButton positioned
    // exactly over the clickable shape, tested with IsItemClicked()
    // instead of raw mouse state. Do the same here, one tiny window per
    // currently-dropped block/pill, positioned to match its own on-screen
    // rect for this frame (same topY/pillY math the draw loop above
    // already uses).
    for (int idx : hoveredIndices)
    {
        const LineSegment& s = segs[idx];
        float depth = s_dropStates[s.key].amount;
        if (depth <= 0.5f) continue; // not visibly dropped yet — nothing to click

        float x0 = s.startX;
        float segEnd = s.endX;
        if (s.endX < screenW) segEnd -= kGapPx;
        if (segEnd <= x0) segEnd = x0 + 1.0f;

        // Edge-safe bounds looked up from dropBoundsByKey (same map the
        // draw loop and PackStackRows use) so the invisible click window
        // lines up with what was actually drawn and with the row that
        // was actually assigned — an edge-widened pill's clickable area
        // must widen with it, not stay pinned to the segment's true
        // (narrower) x-range. Falls back to the raw x0/segEnd if somehow
        // missing (shouldn't happen).
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
        float unsafeRestY = (float)std::max(0, SubscriptionsBarUnsafeHeightPx);
        auto pillIt = pillStackY.find(s.key);
        if (pillIt != pillStackY.end()) unsafeRestY = pillIt->second;
        float topY = baseTopY + (unsafeRestY - baseTopY) * detachT;

        float w = dropX1 - dropX0;
        float h = kMaxDropPx;
        if (w < 1.0f) continue;

        char winId[48];
        snprintf(winId, sizeof(winId), "##we_subbar_click_%d", idx);

        ImGui::SetNextWindowPos(ImVec2(dropX0, topY));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(winId, nullptr,
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoScrollbar     |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground    |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::InvisibleButton("##we_subbar_click_hit", ImVec2(w, h));
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImGui::End();

        if (clicked)
        {
            std::string toCopy = s.chatCode.empty() ? s.name : (s.name + ": " + s.chatCode);
            PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
            io.WantCaptureMouse = true;
            break; // one click, one segment — same as before
        }
    }

}