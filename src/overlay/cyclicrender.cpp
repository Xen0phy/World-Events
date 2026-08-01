//################################################################################
// cyclicrender.cpp
//--------------------------------------------------------------------------------
// RenderCyclicGroups()   draws every CyclicGroup as a clock-face arc on the map
//--------------------------------------------------------------------------------
// Renders the cyclic-event overlay: one ring per CyclicGroup, with a fixed hand
// at "now" and per-slot arcs that fade in/out as their occurrences approach,
// become active, and pass. See RenderCyclicGroups below for the arc geometry
// and dual entry/exit clock model. ArcPoint, ArcSegmentCount, and DrawArc are
// internal helpers for the low-level arc drawing.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "color_utils.h"
#include "cyclicrender.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "map_shared.h"
#include "maprender.h"
#include "settings.h"
#include "time_format.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ArcPoint
//--------------------------------------------------------------------------------
// Point on a circle of given radius at angle_deg. 0 deg = top (12 o'clock),
// increasing clockwise.
//--------------------------------------------------------------------------------
static ImVec2 ArcPoint(ImVec2 center, float radius, float angle_deg)
{
    float rad = (float)((angle_deg - 90.0) * M_PI / 180.0);
    return {
        center.x + radius * cosf(rad),
        center.y + radius * sinf(rad)
    };
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ArcSegmentCount
//--------------------------------------------------------------------------------
// Segment count for tessellating an arc of the given span, sized to keep visual
// error bounded rather than fixed at 1 segment/degree (rings are only 15-40px
// on screen, so a fixed stride wasted most of DrawArc's cost on sub-pixel
// geometry). Uses the same formula Dear ImGui's own AddCircle/PathArcTo use to
// derive a segment count from a max pixel error, scaled from a full circle
// down to just this arc's span.
//--------------------------------------------------------------------------------
static int ArcSegmentCount(float radius, float span_deg)
{
    //_ Same default Dear ImGui uses for AddCircle/PathArcTo
    // (style.CircleTessellationMaxError). Hardcoded, not read from ImGuiStyle,
    // since this project's vendored ImGui predates that field.
    constexpr float maxError = 0.30f;

    float r = fmaxf(radius, 1.0f);
    float segmentsPerCircle = ceilf((float)M_PI / acosf(1.0f - fminf(maxError, r) / r));

    //_ Floor of 12/circle keeps very small rings (heavily zoomed-out map) from
    // faceting visibly - still far below the old 360/circle.
    segmentsPerCircle = fmaxf(segmentsPerCircle, 12.0f);

    int steps = (int)ceilf(segmentsPerCircle * (span_deg / 360.0f));
    return steps < 1 ? 1 : steps;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawArc
//--------------------------------------------------------------------------------
// Draws a thick arc segment counter-clockwise from from_deg to to_deg. Supports
// per-vertex alpha fade (pass alphaFrom=alphaTo for solid color). Uses the
// low-level PrimVtx API so the fade is gapless.
//--------------------------------------------------------------------------------
static void DrawArc(ImDrawList* dl, ImVec2 center, float radius,
    float from_deg, float to_deg,
    ImU32 color, float thickness,
    float alphaFrom = 1.0f, float alphaTo = 1.0f)
{
    float span = from_deg - to_deg;
    if (span <= 0) span += 360.0f;
    if (span <= 0) return;

    //_ Sized off the outer edge (radius + half-thickness), the vertex furthest
    // from true circular, so the whole stroke stays within the error bound.
    int   steps = ArcSegmentCount(radius + thickness * 0.5f, span);
    float step  = span / (float)steps;
    float half  = thickness * 0.5f;
    ImU32 rgb   = color & 0x00FFFFFF;

    //_ Each segment is a quad: 4 vertices, 6 indices.
    dl->PrimReserve(steps * 6, steps * 4);

    for (int i = 0; i < steps; i++)
    {
        float a0 = from_deg - (float)(i    ) * step;
        float a1 = from_deg - (float)(i + 1) * step;
        while (a0 < 0) a0 += 360.0f;
        while (a1 < 0) a1 += 360.0f;

        ImVec2 p0 = ArcPoint(center, radius - half, a0);
        ImVec2 p1 = ArcPoint(center, radius + half, a0);
        ImVec2 p2 = ArcPoint(center, radius + half, a1);
        ImVec2 p3 = ArcPoint(center, radius - half, a1);

        float t0 = (float)(i    ) / (float)steps;
        float t1 = (float)(i + 1) / (float)steps;
        ImU32 c0 = rgb | (((ImU32)((alphaFrom + (alphaTo - alphaFrom) * t0) * 255.0f)) << 24);
        ImU32 c1 = rgb | (((ImU32)((alphaFrom + (alphaTo - alphaFrom) * t1) * 255.0f)) << 24);

        ImVec2 uv = dl->_Data->TexUvWhitePixel;

        //_ Two triangles forming a quad, colors interpolated across vertices.
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 1));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 3));

        dl->PrimWriteVtx(p0, uv, c0);
        dl->PrimWriteVtx(p1, uv, c0);
        dl->PrimWriteVtx(p2, uv, c1);
        dl->PrimWriteVtx(p3, uv, c1);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderCyclicGroups
//--------------------------------------------------------------------------------
// Draws a 270 deg arc per CyclicGroup on the world map. The hand is fixed at
// 0 deg (top, "now"); events rotate counter-clockwise past it. 1 deg = period/360
// seconds. Future events sit CW from the hand out to ARC_FROM=270 deg
// (~75min future, MAX_FUTURE_SECS); past events sit CCW from the hand back to
// ARC_TO=270 deg measured the other way (~15min past, MAX_PAST_SECS), leaving
// a 90 deg gap at the bottom, opposite the hand.
//
// Each slot occurrence is driven by two independent clocks:
//   - EXIT: its own active/past fade-out. A wrapping occurrence (crosses the
//     0s/period boundary) is split into a this-cycle tail + next-cycle head
//     so it renders correctly across the wrap.
//   - ENTRY: the lead-in to that occurrence's NEXT recurrence, computed once
//     per occurrence (not per wrap-pass) from (period - phase). Tracked
//     independently of whether the current occurrence is still active, so
//     the entering arc can appear before the exiting one has fully faded.
//--------------------------------------------------------------------------------

static constexpr float HAND_DEG = 0.0f;   //. top of circle, "now"

void RenderCyclicGroups()
{
    //_ Background list, not foreground: the foreground list composites LAST,
    // after tooltips, so its rings would paint over them. The background list
    // draws FIRST, before every window/tooltip, so tooltips stay on top.
    ImDrawList* dl  = ImGui::GetBackgroundDrawList();
    time_t      now = time(nullptr);

    const float zoomMult  = GetEventZoomSizeMultiplier();
    const float RADIUS    = CyclicRadius    * zoomMult;
    const float THICKNESS = CyclicThickness * zoomMult;
    constexpr ImU32 COL_TRACK = IM_COL32(100, 100, 100, 120);
    constexpr ImU32 COL_HAND  = IM_COL32(255, 255, 255, 240);

    //_ This window always keeps NoMouseInputs. Drag capture is handled
    // per-marker by a small anchor window instead, so this full-screen
    // overlay never blocks map-dragging.
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##we_cyclic_overlay", nullptr,
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoInputs        |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    for (int i = 0; i < (int)g_CyclicGroups.size(); i++)
    {
        CyclicGroup& grp = g_CyclicGroups[i];
        bool isBeingEdited = (g_EditMode.target == EditTarget::CyclicGroup && g_EditMode.index == i);

        //_ Derived from THIS group's own period (Dry Top runs 3600s, most
        // others 7200s) - a single hardcoded value would misdraw any group.
        const float SECS_PER_DEG = (float)grp.period / 360.0f;

        //_ Stored as degrees (CyclicMaxFutureDeg/CyclicMaxPastDeg,
        // settings_table.h); converted to this group's seconds here.
        const float MAX_FUTURE_SECS = CyclicMaxFutureDeg * SECS_PER_DEG;
        const float MAX_PAST_SECS   = CyclicMaxPastDeg   * SECS_PER_DEG;

        //_ ARC_TO is expressed as (360 - past-angle): the per-slot past-fade
        // below sweeps from the hand toward 360, not toward the past-angle.
        const float ARC_FROM = CyclicMaxFutureDeg;
        const float ARC_TO   = 360.0f - CyclicMaxPastDeg;

        ImVec2 pos = ContinentToScreen(grp.continentX, grp.continentY);

        //_ Off-screen culling, skipped for the group currently being dragged.
        if (!isBeingEdited)
        {
            if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
            if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;
        }

        //_ Map-only hide: skips the whole ring if grp.shown is false (exempted
        // while being dragged). For hiding a single slot within an otherwise
        // visible ring, see slot.shown in the loop below.
        if (!isBeingEdited && !grp.shown)
            continue;

        //_ RGB from grp.IdleColor() (see events.h), alpha from COL_TRACK - so
        // the idle track stays translucent/recessed vs. an active slot's arc.
        ImU32 idle     = grp.IdleColor();
        ImU32 colTrack = (idle & 0x00FFFFFF) | (COL_TRACK & 0xFF000000);

        //_ Future portion: solid
        DrawArc(dl, pos, RADIUS, ARC_FROM, HAND_DEG, colTrack, THICKNESS);
        //_ Past portion: fades from full opacity at hand to transparent at exit
        DrawArc(dl, pos, RADIUS, HAND_DEG, ARC_TO,   colTrack, THICKNESS,
                1.0f, 0.0f);

        int secondsOfDay = (int)(now % grp.period);

        for (const auto& slot : grp.slots)
        {
            //_ Hides just this occurrence; the track and other slots still draw.
            if (!slot.shown)
                continue;

            ImU32 color = grp.SlotColor(slot);

            //_ Either offset+repeat (evenly-spaced) or an explicit list
            // (isVarying) - both become "a list of base offsets," so the
            // geometry below doesn't care which produced it.
            std::vector<int> baseOffsets;
            if (slot.isVarying)
            {
                baseOffsets = slot.varyingTimes;
            }
            else
            {
                int repeat  = slot.repeat > 0 ? slot.repeat : 1;
                int subSpan = grp.period / repeat; //. spacing between repeated occurrences
                baseOffsets.reserve(repeat);
                for (int r = 0; r < repeat; r++)
                    baseOffsets.push_back(slot.offset + r * subSpan);
            }

            for (int baseOffset : baseOffsets)
            {
                //_ A wrapping occurrence (crosses the period boundary) is split
                // into a this-cycle tail + next-cycle head so the active/fade
                // segment renders correctly across the 0s/period wrap.
                int slotEnd = baseOffset + slot.duration;
                int wrapDur = slotEnd > grp.period ? slotEnd % grp.period : 0;
                int passes  = wrapDur ? 2 : 1;

                for (int pass = 0; pass < passes; pass++)
                {
                    //_ Pass 0's duration is capped at what actually fits before
                    // the wrap (slot.duration - wrapDur), not (period - baseOffset).
                    int offset   = pass == 0 ? baseOffset : 0;
                    int duration = pass == 0 ? (slot.duration - wrapDur) : wrapDur;
                    if (duration <= 0) continue;

                    int phase         = ((secondsOfDay - offset) % grp.period + grp.period) % grp.period;
                    bool active       = (phase < duration);
                    int secsFromStart = phase;

                    if (active)
                    {
                        //_ Segment straddles the hand: future part solid, past part fades.
                        float futureDeg = fminf((float)(duration - secsFromStart) / SECS_PER_DEG,
                                                ARC_FROM);
                        float pastSecs  = fminf((float)secsFromStart, MAX_PAST_SECS);
                        float pastDeg   = 360.0f - pastSecs / SECS_PER_DEG;

                        if (futureDeg > HAND_DEG)
                            DrawArc(dl, pos, RADIUS, futureDeg, HAND_DEG,
                                    color, THICKNESS);

                        if (pastDeg < 360.0f)
                            DrawArc(dl, pos, RADIUS, HAND_DEG, pastDeg,
                                    color, THICKNESS, 1.0f, 0.0f);
                    }
                }

                //_ Lead-in to this occurrence's NEXT recurrence, computed once per
                // occurrence (not per pass) from (period - phase) - see ENTRY in the
                // function header above for why this runs independently of `active`.
                int basePhase     = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
                int secsUntilNext = grp.period - basePhase;
                if ((float)secsUntilNext <= MAX_FUTURE_SECS)
                {
                    float leadDeg  = (float)secsUntilNext / SECS_PER_DEG;
                    float trailDeg = fminf((float)(secsUntilNext + slot.duration) / SECS_PER_DEG,
                                           ARC_FROM);

                    if (trailDeg > leadDeg)
                        DrawArc(dl, pos, RADIUS, trailDeg, leadDeg, color, THICKNESS);
                }
            }
        }

        //_ Fixed hand at top (0 deg)
        ImVec2 handTip  = ArcPoint(pos, RADIUS + THICKNESS * 0.5f, HAND_DEG);
        ImVec2 handBase = ArcPoint(pos, RADIUS - THICKNESS * 0.5f, HAND_DEG);
        dl->AddLine(handBase, handTip, COL_HAND, 2.0f);

        float hoverR = RADIUS + THICKNESS;

        //_ Drawn just outside the ring's own outer edge (hoverR) so it doesn't
        // compete visually with the arcs/hand inside it. Shared with
        // RenderMapEvents (maprender.cpp) - see overlay/map_shared.h.
        if (isBeingEdited)
            DrawEditPulseRing(dl, pos, hoverR);

        bool hovered = ImGui::IsMouseHoveringRect(
            {pos.x - hoverR, pos.y - hoverR},
            {pos.x + hoverR, pos.y + hoverR});

        //_ Left-drag while armed, via a small anchor window over the ring's
        // rect (shared with RenderMapEvents - see map_shared.h). Stays armed
        // across multiple press-drag-release cycles; only the panel's "Drag"
        // button (now "Stop") ends editing, not mouse release.
        if (isBeingEdited)
            DrawDragAnchor("##we_drag_anchor_cyclic", i, pos, hoverR,
                &grp.continentX, &grp.continentY);

        //_ Tooltip on hover, suppressed while dragging this ring.
        if (hovered && !isBeingEdited)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            ImGui::SetNextWindowPos(
                {mouse.x - 1.0f, mouse.y - 20.0f},
                ImGuiCond_Always, {0.0f, 1.0f});
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(grp.name.c_str());
            ImGui::Separator();

            //********************************************************************************
            // TooltipEntry
            //--------------------------------------------------------------------------------
            // name    slot/event name shown in the tooltip
            // active  true if currently active, false if upcoming
            // secs    secsLeft if active, secsUntilStart if upcoming
            //--------------------------------------------------------------------------------
            // One entry per unique slot NAME across the whole group, not per Slot
            // struct - two Slots sharing a name (e.g. Dry Top's two "Crash Site"
            // slots at different offsets) are one event as far as the user needs
            // to know on hover. The ring still draws every occurrence regardless;
            // this collapsing is tooltip-text only.
            //--------------------------------------------------------------------------------
            struct TooltipEntry
            {
                std::string name;
                bool        active;
                int         secs;
            };

            std::unordered_map<std::string, TooltipEntry> byName;   //. keyed by slot name

            for (const auto& slot : grp.slots)
            {
                //_ Same as the drawing loop above: a hidden slot shouldn't
                // leak into the tooltip just because its arc is suppressed.
                if (!slot.shown)
                    continue;

                std::vector<int> baseOffsets;
                if (slot.isVarying)
                {
                    baseOffsets = slot.varyingTimes;
                }
                else
                {
                    int repeat  = slot.repeat > 0 ? slot.repeat : 1;
                    int subSpan = grp.period / repeat;
                    baseOffsets.reserve(repeat);
                    for (int r = 0; r < repeat; r++)
                        baseOffsets.push_back(slot.offset + r * subSpan);
                }

                bool foundActive    = false;
                int  activeSecsLeft = 0;
                bool foundUpcoming  = false;
                int  bestSecsUntil  = grp.period;

                for (int baseOffset : baseOffsets)
                {
                    int phase          = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
                    bool active        = (phase < slot.duration);
                    int secsUntilStart = active ? 0 : (grp.period - phase);

                    if (active)
                    {
                        foundActive    = true;
                        activeSecsLeft = slot.duration - phase;
                        break; //. a slot can't be active in two repeats at once
                    }
                    else if (secsUntilStart < bestSecsUntil)
                    {
                        foundUpcoming = true;
                        bestSecsUntil = secsUntilStart;
                    }
                }

                if (!foundActive && !foundUpcoming)
                    continue;

                TooltipEntry candidate = foundActive
                    ? TooltipEntry{ slot.name, true, activeSecsLeft }
                    : TooltipEntry{ slot.name, false, bestSecsUntil };

                auto it = byName.find(slot.name);
                if (it == byName.end())
                {
                    byName.emplace(slot.name, candidate);
                }
                else
                {
                    //_ Reduce against whatever's already there for this name:
                    // active beats upcoming; otherwise soonest wins.
                    TooltipEntry& existing = it->second;
                    bool candidateWins =
                        (candidate.active && !existing.active) ||
                        (candidate.active == existing.active && candidate.secs < existing.secs);
                    if (candidateWins)
                        existing = candidate;
                }
            }

            std::vector<TooltipEntry> entries;
            entries.reserve(byName.size());
            for (const auto& kv : byName)
                entries.push_back(kv.second);

            std::sort(entries.begin(), entries.end(), [](const TooltipEntry& a, const TooltipEntry& b)
            {
                if (a.active != b.active) return a.active; //. active entries first
                return a.secs < b.secs;                    //. then soonest first
            });

            for (const auto& e : entries)
            {
                //_ Same status swatches/threshold as the Basic Events map
                // dots (maprender.cpp) - active/soon/waiting - so ring
                // tooltips match the rest of the overlay.
                if (e.active)
                {
                    ImGui::TextColored(ToImVec4(BasicEventColorActive),
                        "%s — Active (ends in %s)",
                        e.name.c_str(), FormatMinSec(e.secs).c_str());
                }
                else
                {
                    const float* col = e.secs < 900 ? BasicEventColorSoon
                                                      : BasicEventColorWaiting;
                    ImGui::TextColored(ToImVec4(col),
                        "%s — in %s",
                        e.name.c_str(), FormatCountdown(e.secs).c_str());
                }
            }

            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}