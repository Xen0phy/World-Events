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
};

// ---------------------------------------------------------------------------
// CollectVisibleSegments
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
// ---------------------------------------------------------------------------
struct DropState { float amount = 0.0f; }; // eased 0..1 drop amount
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

    // ---- Layout constants (local space: y=0 is the baseline strip itself,
    // dropped blocks extend DOWNWARD from there since the line lives on
    // the top edge) ----
    constexpr float kLineThick   = 2.0f;
    constexpr float kBaselineY   = kLineThick * 0.5f;  // line is centered on this y, so offsetting by half its thickness puts the stroke's visible top edge flush with the actual screen edge instead of half of it getting clipped off-screen
    constexpr float kMaxDropPx   = 54.0f; // how far a fully-hovered block drops down from the baseline — sized to comfortably fit two centered lines of label text
    constexpr float kGapPx       = 3.0f;  // thin background-colored notch between adjacent segments

    ImVec2 mouse = io.MousePos;
    bool mouseValid = io.MousePos.x > -FLT_MAX; // ImGui reports (-FLT_MAX,-FLT_MAX) when there's no mouse

    // Which segment (if any) is currently under the mouse, in the thin
    // hover band right along the baseline — matches isTouching() in the
    // HTML reference, simplified since this strip has no existing drop to
    // extend the hit-test region downward (a subscription overlay doesn't
    // need to keep tracking the mouse into an already-dropped block the
    // way the interactive demo does).
    constexpr float kHoverBand = 10.0f;
    int hoveredIdx = -1;
    if (mouseValid && mouse.y >= kBaselineY - kHoverBand && mouse.y <= kBaselineY + kHoverBand)
    {
        for (int i = 0; i < (int)segs.size(); i++)
        {
            if (mouse.x >= segs[i].startX && mouse.x < segs[i].endX) { hoveredIdx = i; break; }
        }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ---- Ease every segment's drop amount toward its target this frame ----
    float dt = io.DeltaTime > 0.0f ? std::min(io.DeltaTime, 0.1f) : 0.0f;
    // Frame-rate-independent version of the HTML's fixed *=0.12 lerp-per-
    // frame: raise the per-frame rate to a power of (dt * 60) so the same
    // half-life holds regardless of the addon's actual frame rate.
    float easeThisFrame = 1.0f - powf(1.0f - kEaseRate, dt > 0.0f ? dt * 60.0f : 1.0f);

    for (int i = 0; i < (int)segs.size(); i++)
    {
        float target = (i == hoveredIdx) ? 1.0f : 0.0f;
        DropState& st = s_dropStates[segs[i].key];
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

    // ---- Baseline: thin line across the full screen width, tinted by
    // whichever segment (if any) is currently dropped, else a neutral
    // dim white — matches the HTML's per-segment-colored stroke copies
    // of the shared baseline path. ----
    dl->AddLine(ImVec2(0, kBaselineY), ImVec2(screenW, kBaselineY),
        IM_COL32(255, 255, 255, 90), kLineThick);

    // ---- Per-segment colored baseline overlay + dropped block ----
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

        // Colored baseline segment (always visible, even at depth 0) —
        // this is what makes the strip read as "N colored ticks" at a
        // glance before the user ever hovers anything.
        dl->AddLine(ImVec2(x0, kBaselineY), ImVec2(segEnd, kBaselineY), segColor, kLineThick + 1.0f);

        if (depth > 0.002f)
        {
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
            dl->PathLineTo(ImVec2(left, kBaselineY));
            for (int s = 0; s <= samples; s++)
            {
                float x = left + (right - left) * (s / (float)samples);
                float d = FlatBlockDepthAt(x, x0, segEnd, tw) * depth;
                float y = kBaselineY + d * kMaxDropPx;
                dl->PathLineTo(ImVec2(x, y));
            }
            dl->PathLineTo(ImVec2(right, kBaselineY));
            dl->PathFillConvex(fillColor);

            // Re-stroke the curve's top edge on top of the fill for a
            // crisp outline, same colored-stroke-over-fill layering as
            // the HTML's lineGroup drawn above fillGroup.
            dl->PathClear();
            for (int s = 0; s <= samples; s++)
            {
                float x = left + (right - left) * (s / (float)samples);
                float d = FlatBlockDepthAt(x, x0, segEnd, tw) * depth;
                float y = kBaselineY + d * kMaxDropPx;
                dl->PathLineTo(ImVec2(x, y));
            }
            dl->PathStroke(segColor, false, kLineThick);

            // Label + status, centered under the deepest point of the
            // drop, fading in with depth so it doesn't pop in abruptly.
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

                // Vertically center both lines inside the dropped block
                // itself (between the baseline and the current drop
                // depth), rather than placing them below it — matches
                // the HTML reference, where the label lives inside the
                // filled shape, not underneath it. Uses the drop's
                // steady-state depth (kMaxDropPx) for the layout math so
                // the text doesn't visibly slide as depth eases toward
                // 1.0 — it simply fades in in place via alpha instead.
                float cx = (x0 + segEnd) * 0.5f;
                float blockTop    = kBaselineY;
                float blockBottom = kBaselineY + kMaxDropPx;
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
    // affordance as a row-click in the text watchlist window — only
    // claims the mouse for this one instant, same WantCaptureMouse
    // convention as RenderMapEvents/RenderCyclicGroups.
    if (hoveredIdx >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const LineSegment& seg = segs[hoveredIdx];
        std::string toCopy = seg.chatCode.empty() ? seg.name : (seg.name + ": " + seg.chatCode);
        PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
        io.WantCaptureMouse = true;
    }
}