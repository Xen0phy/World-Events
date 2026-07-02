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
#include <ctime>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>
#include <unordered_map>
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
    ImU32       color;
    int         lane = 0;   // 0 = this segment is the one drawn on the single resting baseline for its time range; >0 = this segment is currently hidden behind a lane-0 segment and only shows up as a dot marker + on hover — see AssignLanes
};

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
        segs.push_back({
            "Basic:" + it->name, it->name, it->chatCode,
            secToX(startSec), secToX(endSec), active, statusSecs,
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

            char offsetBuf[16];
            snprintf(offsetBuf, sizeof(offsetBuf), "%d", key.slotOffset);
            std::string label = it->name + " - " + slot.name;
            segs.push_back({
                "Cyclic:" + it->name + ":" + offsetBuf, label, slot.chatCode,
                secToX(startSec), secToX(endSec), active, statusSecs,
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
// Depth profile (0..1) of a single "dropped" block at local-x position x:
// flat 0 before start-tw, eases up across [start-tw, start], flat 1
// across [start, end], eases back down across [end, end+tw]. Direct port
// of flatBlockDepthAt() in distribution-line.html.
// ---------------------------------------------------------------------------
static float FlatBlockDepthAt(float x, float start, float end, float tw)
{
    if (x < start - tw) return 0.0f;
    if (x < start)       return SmoothStep((x - (start - tw)) / tw);
    if (x <= end)         return 1.0f;
    if (x <= end + tw)    return SmoothStep(1.0f - (x - end) / tw);
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

static constexpr float kTransitionWidth = 26.0f; // px — curved shoulder width, scaled down from the HTML's 60px for a much thinner overlay strip
static constexpr float kEaseRate        = 0.18f; // per-frame lerp factor, matches the HTML's drop easing constant

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
    constexpr float kMaxDropPx    = 54.0f; // how far a single fully-hovered block drops down from the baseline — sized to comfortably fit two centered lines of label text
    constexpr float kGapPx        = 3.0f;  // thin background-colored notch between adjacent lane-0 segments
    constexpr float kStackGapPx   = 4.0f;  // vertical gap between stacked dropped blocks when multiple segments are hovered at once
    constexpr float kDotRadius    = 2.5f;  // px
    constexpr float kDotSpacingPx = 7.0f;  // horizontal spacing between two dots that land on the exact same tick, so a cluster reads as "several dots" rather than one blob
    constexpr float kDotY         = kBaselineY + 8.0f; // dots sit a small, fixed distance below the baseline — not tied to any lane, since lanes no longer draw their own resting line
    constexpr float kDotHitRadius = 5.0f;  // generous click/hover target around each dot's visual radius

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
    //  1. The mouse is over a lane-0 segment's own resting-line x-range
    //     (near the baseline y) — same as before.
    //  2. The mouse is over one of that segment's dot markers (a lane>0
    //     segment has no line of its own to hover, only its dot) — hit-
    //     tested as a small circle around the dot's nudged draw position.
    // Both feed into the same hoveredIndices list, so a hidden segment
    // dropping in via its dot stacks together with the shown segment
    // exactly like two lane-0 segments would.
    // Vertical hover band matches the drawn line's actual on-screen
    // footprint (kLineThick + 1px, per the AddLine call below) plus a
    // couple px of forgiveness — NOT a generous fixed band. The line is
    // only ~3px tall; a much taller invisible hit zone around it makes
    // the pop-out trigger from empty space above/below the line, which
    // reads as broken.
    constexpr float kLineHalfHeight = (kLineThick + 1.0f) * 0.5f;
    constexpr float kHoverBand = kLineHalfHeight + 2.0f;
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
    std::sort(hoveredIndices.begin(), hoveredIndices.end(), [&](int a, int b)
    {
        return segs[a].statusSecs < segs[b].statusSecs;
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
    std::unordered_map<std::string, float> stackTopY;
    {
        float runningY = kBaselineY;
        for (int idx : hoveredIndices)
        {
            const LineSegment& s = segs[idx];
            float depth = s_dropStates[s.key].amount;
            stackTopY[s.key] = runningY;
            runningY += kMaxDropPx * depth + kStackGapPx * depth;
        }
    }

    // ---- Per-segment colored baseline overlay (lane-0 only) + dropped
    // block (any segment currently easing toward/away from a hover,
    // lane-0 or not). ----
    for (int i = 0; i < (int)segs.size(); i++)
    {
        const LineSegment& seg = segs[i];
        float depth = s_dropStates[seg.key].amount;

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
            // animating back down) just ease from the shared baseline —
            // lane>0 segments have no resting y of their own to ease
            // from, so they drop from/return to the same baseline every
            // lane-0 segment uses.
            float topY = kBaselineY;
            auto stackIt = stackTopY.find(seg.key);
            if (stackIt != stackTopY.end()) topY = stackIt->second;

            // Build the smooth dropped-block silhouette by sampling
            // FlatBlockDepthAt across [x0 - tw, segEnd + tw] and filling
            // the polygon baseline -> curve -> baseline, same shape the
            // HTML builds via its sampled quadratic-Bezier path, just
            // filled directly as an ImGui convex-ish polygon instead of
            // an SVG <path> — PathFillConvex handles the smooth silhouette
            // fine since the curve is monotonic-ish per shoulder and
            // never folds back on itself.
            float tw = kTransitionWidth;
            float left  = std::max(0.0f, x0 - tw);
            float right = std::min(screenW, segEnd + tw);

            int samples = std::max(8, (int)((right - left) / 4.0f));

            dl->PathClear();
            dl->PathLineTo(ImVec2(left, topY));
            for (int s = 0; s <= samples; s++)
            {
                float x = left + (right - left) * (s / (float)samples);
                float d = FlatBlockDepthAt(x, x0, segEnd, tw) * depth;
                float y = topY + d * kMaxDropPx;
                dl->PathLineTo(ImVec2(x, y));
            }
            dl->PathLineTo(ImVec2(right, topY));
            dl->PathFillConvex(fillColor);

            // Re-stroke the curve's top edge on top of the fill for a
            // crisp outline, same colored-stroke-over-fill layering as
            // the HTML's lineGroup drawn above fillGroup.
            dl->PathClear();
            for (int s = 0; s <= samples; s++)
            {
                float x = left + (right - left) * (s / (float)samples);
                float d = FlatBlockDepthAt(x, x0, segEnd, tw) * depth;
                float y = topY + d * kMaxDropPx;
                dl->PathLineTo(ImVec2(x, y));
            }
            dl->PathStroke(segColor, false, kLineThick);

            // Label + status, vertically centered inside this block's own
            // slice of the stack, fading in with depth so it doesn't pop
            // in abruptly.
            if (depth > 0.35f)
            {
                char statusBuf[48];
                if (seg.active)
                    snprintf(statusBuf, sizeof(statusBuf), "Active - ends in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);
                else
                    snprintf(statusBuf, sizeof(statusBuf), "in %dm %02ds", seg.statusSecs / 60, seg.statusSecs % 60);

                std::string line1 = seg.name;
                std::string line2 = statusBuf;
                ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
                ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());

                // Vertically center both lines inside this block's own
                // slot in the stack (between topY and topY+kMaxDropPx),
                // rather than placing them below it — matches the HTML
                // reference, where the label lives inside the filled
                // shape, not underneath it. Uses the drop's steady-state
                // depth for the layout math so text doesn't visibly slide
                // as depth eases toward 1.0 — it fades in in place instead.
                float cx = (x0 + segEnd) * 0.5f;
                float blockTop    = topY;
                float blockBottom = topY + kMaxDropPx;
                float textBlockH  = size1.y + size2.y;
                float labelY      = blockTop + (blockBottom - blockTop - textBlockH) * 0.5f;

                float alpha = (depth - 0.35f) / 0.65f;
                ImU32 textCol   = IM_COL32(255, 255, 255, (int)(230 * alpha));
                ImU32 shadowCol = IM_COL32(0, 0, 0, (int)(180 * alpha));

                // cheap 1px drop shadow for legibility over arbitrary game backgrounds
                dl->AddText(ImVec2(cx - size1.x * 0.5f + 1, labelY + 1), shadowCol, line1.c_str());
                dl->AddText(ImVec2(cx - size1.x * 0.5f, labelY), textCol, line1.c_str());
                dl->AddText(ImVec2(cx - size2.x * 0.5f + 1, labelY + size1.y + 1), shadowCol, line2.c_str());
                dl->AddText(ImVec2(cx - size2.x * 0.5f, labelY + size1.y), textCol, line2.c_str());
            }
        }
    }

    // Click on a hovered/dropped segment copies its waypoint code, same
    // affordance as a row-click in the text watchlist window. Only
    // segments that have actually finished (or nearly finished) dropping
    // are eligible — clicking during the hover-delay window, before
    // there's any visible block to click on, shouldn't silently copy
    // something the user can't see yet. With multiple segments possibly
    // stacked at once, pick whichever segment's stacked block the mouse
    // y actually falls within (falls back to the topmost/soonest
    // eligible one if the pointer's between blocks or still on the thin
    // baseline itself) — only claims the mouse for this one instant,
    // same WantCaptureMouse convention as RenderMapEvents/
    // RenderCyclicGroups.
    std::vector<int> droppedIndices;
    for (int idx : hoveredIndices)
        if (s_dropStates[segs[idx].key].amount > 0.5f) droppedIndices.push_back(idx);

    if (!droppedIndices.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        int clickIdx = droppedIndices[0];
        for (int idx : droppedIndices)
        {
            float topY = stackTopY[segs[idx].key];
            if (mouse.y >= topY && mouse.y <= topY + kMaxDropPx) { clickIdx = idx; break; }
        }
        const LineSegment& seg = segs[clickIdx];
        std::string toCopy = seg.chatCode.empty() ? seg.name : (seg.name + ": " + seg.chatCode);
        PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
        io.WantCaptureMouse = true;
    }
}
