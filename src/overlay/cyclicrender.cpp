//################################################################################
// cyclicrender.cpp
//--------------------------------------------------------------------------------
// RenderCyclicGroups()   draws every CyclicGroup as a clock-face arc on the map
//--------------------------------------------------------------------------------
// Renders the cyclic-event overlay: one ring per CyclicGroup, with a fixed hand
// at "now" and per-slot arcs that fade in/out as their occurrences approach,
// become active, and pass. See RenderCyclicGroups below for the arc geometry and
// dual entry/exit clock model. ArcPoint, ArcSegmentCount, DrawArc, and
// DrawArcImage are internal helpers for the low-level arc/ring-image drawing.
//--------------------------------------------------------------------------------

#include <algorithm>

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
// error bounded (rings are only 15-40px on screen, so a fixed stride wasted most
// of DrawArc's cost on sub-pixel geometry). Uses the same formula Dear ImGui's
// own AddCircle/PathArcTo use to derive a segment count from a max pixel error,
// scaled from a full circle down to just this arc's span.
//--------------------------------------------------------------------------------
static int ArcSegmentCount(float radius, float span_deg)
{
    //_ Same as Dear ImGui's own default; vendored copy predates that field.
    constexpr float maxError = 0.30f;

    float r = fmaxf(radius, 1.0f);
    float segmentsPerCircle = ceilf((float)M_PI / acosf(1.0f - fminf(maxError, r) / r));

    //_ Floor of 12/circle avoids visible faceting on heavily zoomed-out rings.
    segmentsPerCircle = fmaxf(segmentsPerCircle, 12.0f);

    int steps = (int)ceilf(segmentsPerCircle * (span_deg / 360.0f));
    return steps < 1 ? 1 : steps;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ArcSpanDeg
//--------------------------------------------------------------------------------
// Same from_deg/to_deg -> span wrap-handling every arc-drawing helper here needs
// (DrawArc inlines its own copy; DrawArcImage and RenderCyclicGroups' image-
// splitting below share this one). from_deg==to_deg resolves to span 0, not a
// full 360 deg circle - every caller here draws a bounded track/fade/slot arc,
// never a degenerate from==to meaning "draw the whole ring". A genuine negative
// span (to_deg only reachable by wrapping past 360) still gets the +360 wrap;
// only the exact from==to case is forced to 0.
//--------------------------------------------------------------------------------
static float ArcSpanDeg(float from_deg, float to_deg)
{
    float span = from_deg - to_deg;
    if (span < 0) span += 360.0f;
    if (span < 0) span = 0.0f;
    return span;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawArc
//--------------------------------------------------------------------------------
// Draws a thick arc segment counter-clockwise from from_deg to to_deg. Supports
// per-vertex alpha fade (pass alphaFrom=alphaTo for solid color). Uses the low-
// level PrimVtx API so the fade is gapless.
//--------------------------------------------------------------------------------
static void DrawArc(ImDrawList* dl, ImVec2 center, float radius,
    float from_deg, float to_deg,
    ImU32 color, float thickness,
    float alphaFrom = 1.0f, float alphaTo = 1.0f)
{
    float span = ArcSpanDeg(from_deg, to_deg);
    if (span <= 0) return;

    //_ Sized off the outer edge (radius + half-thickness), for the error bound.
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
// DrawArcImage
//--------------------------------------------------------------------------------
// Wraps a texture around one ring edge, along the same from_deg->to_deg arc
// geometry as DrawArc, so the image always covers exactly the arc portion drawn.
// Image is STRETCHED (not tiled): U runs uStart->uEnd; RenderCyclicGroups calls
// this twice per edge (future, then past), passing each its own 0..1 slice so the
// wrap stays seamless across the HAND_DEG seam. alphaFrom/alphaTo fade per-vertex
// like DrawArc's own fade; default (1,1) draws solid, untinted. V spans the
// band's own thickness; flipV mirrors it so the same source art faces outward on
// the outer edge and inward on the (V-flipped) inner edge.
//--------------------------------------------------------------------------------
static void DrawArcImage(ImDrawList* dl, ImTextureID tex, ImVec2 center,
    float edgeRadius, float halfHeight,
    float from_deg, float to_deg,
    bool flipV,
    float uStart = 0.0f, float uEnd = 1.0f,
    float alphaFrom = 1.0f, float alphaTo = 1.0f)
{
    float span = ArcSpanDeg(from_deg, to_deg);
    if (span <= 0) return;

    //_ Same error-bounded tessellation as DrawArc, off the band's outer edge.
    int   steps = ArcSegmentCount(edgeRadius + halfHeight, span);
    float step  = span / (float)steps;

    float vNear = flipV ? 1.0f : 0.0f;   //. V at edgeRadius - halfHeight
    float vFar  = flipV ? 0.0f : 1.0f;   //. V at edgeRadius + halfHeight

    constexpr ImU32 rgb = 0x00FFFFFF;   //. white, untinted

    //_ Binds tex; PrimReserve/PrimWrite* below fill this draw command directly.
    dl->PushTextureID(tex);
    dl->PrimReserve(steps * 6, steps * 4);

    for (int i = 0; i < steps; i++)
    {
        float a0 = from_deg - (float)(i    ) * step;
        float a1 = from_deg - (float)(i + 1) * step;
        while (a0 < 0) a0 += 360.0f;
        while (a1 < 0) a1 += 360.0f;

        ImVec2 p0 = ArcPoint(center, edgeRadius - halfHeight, a0);
        ImVec2 p1 = ArcPoint(center, edgeRadius + halfHeight, a0);
        ImVec2 p2 = ArcPoint(center, edgeRadius + halfHeight, a1);
        ImVec2 p3 = ArcPoint(center, edgeRadius - halfHeight, a1);

        float t0 = (float)(i    ) / (float)steps;
        float t1 = (float)(i + 1) / (float)steps;

        float u0 = uStart + (uEnd - uStart) * t0;
        float u1 = uStart + (uEnd - uStart) * t1;

        ImU32 c0 = rgb | (((ImU32)((alphaFrom + (alphaTo - alphaFrom) * t0) * 255.0f)) << 24);
        ImU32 c1 = rgb | (((ImU32)((alphaFrom + (alphaTo - alphaFrom) * t1) * 255.0f)) << 24);

        //_ Two triangles forming a quad, same winding as DrawArc.
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 1));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 3));

        dl->PrimWriteVtx(p0, ImVec2(u0, vNear), c0);
        dl->PrimWriteVtx(p1, ImVec2(u0, vFar),  c0);
        dl->PrimWriteVtx(p2, ImVec2(u1, vFar),  c1);
        dl->PrimWriteVtx(p3, ImVec2(u1, vNear), c1);
    }

    dl->PopTextureID();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawArcTextureOverlay
//--------------------------------------------------------------------------------
// Lays a texture "decal" over the ring's own fill (track + slot arcs) for grain,
// per CyclicFillImage* (settings_table.h). Unlike DrawArcImage - which wraps its
// source image around the arc (U along from_deg->to_deg) - this computes UV from
// each vertex's offset from `center`, independent of the arc parameter: it reads
// as a decal held centered behind the ring. `projRadius` (the ring's own outer
// radius) scales the decal with Radius/Thickness/zoom, avoiding drift out of
// registration. alpha is a flat multiplier (CyclicFillImageOpacity), no tint or
// per-vertex fade like DrawArc/DrawArcImage - it reads as texture on top of
// whatever's already there, not its own fading element.
//--------------------------------------------------------------------------------
static void DrawArcTextureOverlay(ImDrawList* dl, ImTextureID tex, ImVec2 center,
    float radius, float halfHeight,
    float from_deg, float to_deg,
    float projRadius, float alpha)
{
    float span = ArcSpanDeg(from_deg, to_deg);
    if (span <= 0 || alpha <= 0.0f || projRadius <= 0.0f) return;

    int   steps = ArcSegmentCount(radius + halfHeight, span);
    float step  = span / (float)steps;
    ImU32 col   = IM_COL32(255, 255, 255, (int)(alpha * 255.0f + 0.5f));

    //_ UV = offset from center, in units of the ring's outer diameter.
    auto uvOf = [&](ImVec2 p) -> ImVec2
    {
        return { 0.5f + (p.x - center.x) / (2.0f * projRadius),
                 0.5f + (p.y - center.y) / (2.0f * projRadius) };
    };

    dl->PushTextureID(tex);
    dl->PrimReserve(steps * 6, steps * 4);

    for (int i = 0; i < steps; i++)
    {
        float a0 = from_deg - (float)(i    ) * step;
        float a1 = from_deg - (float)(i + 1) * step;
        while (a0 < 0) a0 += 360.0f;
        while (a1 < 0) a1 += 360.0f;

        ImVec2 p0 = ArcPoint(center, radius - halfHeight, a0);
        ImVec2 p1 = ArcPoint(center, radius + halfHeight, a0);
        ImVec2 p2 = ArcPoint(center, radius + halfHeight, a1);
        ImVec2 p3 = ArcPoint(center, radius - halfHeight, a1);

        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 1));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 0));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
        dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 3));

        dl->PrimWriteVtx(p0, uvOf(p0), col);
        dl->PrimWriteVtx(p1, uvOf(p1), col);
        dl->PrimWriteVtx(p2, uvOf(p2), col);
        dl->PrimWriteVtx(p3, uvOf(p3), col);
    }

    dl->PopTextureID();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderCyclicGroups
//--------------------------------------------------------------------------------
// Draws a 270 deg arc per CyclicGroup on the world map. Hand fixed at 0 deg
// ("now"); events rotate counter-clockwise past it, 1 deg = period/360 seconds.
// Future events run CW to ARC_FROM=270 deg; past events run CCW to ARC_TO=270 deg
// the other way, leaving a 90 deg gap opposite the hand.
//
// Each slot occurrence's phase is computed once via modulo against grp.period,
// from its TRUE baseOffset/slot.duration, wrapping correctly even when duration
// crosses the 0s/period reset. EXIT is that phase's own active/past fade-out;
// ENTRY is the lead-in to the NEXT recurrence (period - phase), tracked
// independently of `active` so it can appear before EXIT fully fades.
//--------------------------------------------------------------------------------

static constexpr float HAND_DEG = 0.0f;   //. top of circle, "now"

void RenderCyclicGroups()
{
    //_ Background list draws first (before tooltips) so they stay on top.
    ImDrawList* dl  = ImGui::GetBackgroundDrawList();
    time_t      now = time(nullptr);

    const float zoomMult  = GetEventZoomSizeMultiplier();
    const float RADIUS    = CyclicRadius    * zoomMult;
    const float THICKNESS = CyclicThickness * zoomMult;
    constexpr ImU32 COL_TRACK = IM_COL32(100, 100, 100, 120);
    const ImU32 COL_HAND = ColorU32(CyclicHandColor); //. user-adjustable, settings_table.h

    //_ Resolved once per group (shared setting); nullptr skips it below.
    Texture_t* ringImg = (CyclicRingImageEnabled && !CyclicRingImageFilename.empty())
        ? GetOrRequestEventIcon(CyclicRingImageFilename)
        : nullptr;
    const bool drawRingImg = ringImg && ringImg->Resource;

    //_ Same resolve/cache scheme as ringImg, for the hand image.
    Texture_t* handImg = (CyclicHandImageEnabled && !CyclicHandImageFilename.empty())
        ? GetOrRequestEventIcon(CyclicHandImageFilename)
        : nullptr;
    const bool drawHandImg = handImg && handImg->Resource;

    //_ Same resolve/cache scheme, for the fill decal (see above).
    Texture_t* fillImg = (CyclicFillImageEnabled && !CyclicFillImageFilename.empty())
        ? GetOrRequestEventIcon(CyclicFillImageFilename)
        : nullptr;
    const bool drawFillImg = fillImg && fillImg->Resource;

    //_ Keeps NoMouseInputs; drag uses a small per-marker anchor window.
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

        //_ THIS group's own period - a fixed value would misdraw others.
        const float SECS_PER_DEG = (float)grp.period / 360.0f;

        //_ Stored as degrees (settings_table.h); converted to seconds here.
        const float MAX_FUTURE_SECS = CyclicMaxFutureDeg * SECS_PER_DEG;
        const float MAX_PAST_SECS   = CyclicMaxPastDeg   * SECS_PER_DEG;

        //_ Continuous 1->0 fade across the whole past window by seconds-behind-hand, not each arc's own span.
        auto PastAlpha = [&](float secsBehindHand) -> float
        {
            if (!CyclicPastFadeEnabled) return 1.0f;
            if (MAX_PAST_SECS <= 0.0f) return 0.0f;
            float t = fminf(fmaxf(secsBehindHand, 0.0f), MAX_PAST_SECS) / MAX_PAST_SECS;
            return 1.0f - t;
        };

        //_ Left unwrapped - wrapping misreads a 360 deg past window as empty.
        const float ARC_FROM = CyclicMaxFutureDeg;
        const float ARC_TO   = HAND_DEG - CyclicMaxPastDeg;

        ImVec2 pos = ContinentToScreen(grp.continentX, grp.continentY);

        //_ Off-screen culling, skipped for the group currently being dragged.
        if (!isBeingEdited)
        {
            if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
            if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;
        }

        //_ Map-only hide (grp.shown); slot.shown hides individual slots below.
        if (!isBeingEdited && !grp.shown)
            continue;

        //_ RGB from IdleColor(); alpha from COL_TRACK keeps the idle track dim.
        ImU32 idle     = grp.IdleColor();
        ImU32 colTrack = (idle & 0x00FFFFFF) | (COL_TRACK & 0xFF000000);

        //_ Future portion: solid
        DrawArc(dl, pos, RADIUS, ARC_FROM, HAND_DEG, colTrack, THICKNESS);
        //_ Fades to transparent at exit unless CyclicPastFadeEnabled is off.
        DrawArc(dl, pos, RADIUS, HAND_DEG, ARC_TO,   colTrack, THICKNESS,
                1.0f, CyclicPastFadeEnabled ? 0.0f : 1.0f);

        int secondsOfDay = (int)(now % grp.period);

        //_ Longest slot first, so shorter overlapping sub-events stay on top.
        std::vector<const CyclicGroup::Slot*> orderedSlots;
        orderedSlots.reserve(grp.slots.size());
        for (const auto& slot : grp.slots)
            orderedSlots.push_back(&slot);
        std::stable_sort(orderedSlots.begin(), orderedSlots.end(),
            [](const CyclicGroup::Slot* a, const CyclicGroup::Slot* b) {
                return a->duration > b->duration;
            });

        for (const auto* slotPtr : orderedSlots)
        {
            const auto& slot = *slotPtr;
            //_ Hides just this occurrence; the track and other slots still draw.
            if (!slot.shown)
                continue;

            ImU32 color = grp.SlotColor(slot);

            //_ offset+repeat or an explicit list - both become base offsets.
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
                //_ Continuous phase via modulo - wraps across the reset.
                int phase         = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
                bool active       = (phase < slot.duration);
                int secsFromStart = phase;

                //_ Time since it ended (0 while active), via its own modulo.
                int secsSinceEnd = ((secondsOfDay - (baseOffset + slot.duration)) % grp.period
                                    + grp.period) % grp.period;

                if (active)
                {
                    //_ Straddles the hand: future solid, past (behind it) fades.
                    float futureDeg = fminf((float)(slot.duration - secsFromStart) / SECS_PER_DEG,
                                            ARC_FROM);
                    float pastSecs  = fminf((float)secsFromStart, MAX_PAST_SECS);
                    float pastDeg   = 360.0f - pastSecs / SECS_PER_DEG;

                    if (futureDeg > HAND_DEG)
                        DrawArc(dl, pos, RADIUS, futureDeg, HAND_DEG,
                                color, THICKNESS);

                    if (pastDeg < 360.0f)
                        DrawArc(dl, pos, RADIUS, HAND_DEG, pastDeg,
                                color, THICKNESS,
                                PastAlpha(0.0f), PastAlpha(pastSecs));
                }

                //_ Independent of active - it's the PREVIOUS iteration's end.
                if (secsSinceEnd >= 0 && (float)secsSinceEnd <= MAX_PAST_SECS)
                {
                    //_ Spans the range this occurrence ran, not from the hand.
                    float nearSecs = fminf((float)secsSinceEnd, MAX_PAST_SECS);
                    float farSecs  = fminf((float)secsSinceEnd + slot.duration, MAX_PAST_SECS);
                    float nearDeg  = 360.0f - nearSecs / SECS_PER_DEG;
                    float farDeg   = 360.0f - farSecs  / SECS_PER_DEG;

                    //_ ArcSpanDeg already guards zero/negative spans here.
                    DrawArc(dl, pos, RADIUS, nearDeg, farDeg,
                            color, THICKNESS,
                            PastAlpha(nearSecs), PastAlpha(farSecs));
                }

                //_ Lead-in to NEXT recurrence - see ENTRY in header above.
                int secsUntilNext = grp.period - phase;
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

        //_ Decal clipped to where the ring is drawn; drawn before hand/edge.
        if (drawFillImg)
        {
            float outerR = RADIUS + THICKNESS * 0.5f;

            //_ ARC_FROM==ARC_TO here means a full circle, not empty.
            bool fullCircle = fabsf(fmodf(CyclicMaxFutureDeg + CyclicMaxPastDeg, 360.0f)) < 0.01f
                               && (CyclicMaxFutureDeg + CyclicMaxPastDeg) > 0.0f;

            if (fullCircle)
                DrawArcTextureOverlay(dl, (ImTextureID)fillImg->Resource, pos,
                    RADIUS, THICKNESS * 0.5f, 360.0f, 0.0f,
                    outerR, CyclicFillImageOpacity);
            else
                DrawArcTextureOverlay(dl, (ImTextureID)fillImg->Resource, pos,
                    RADIUS, THICKNESS * 0.5f, ARC_FROM, ARC_TO,
                    outerR, CyclicFillImageOpacity);
        }

        //_ Fixed hand at top: an image if configured, else the plain tick line.
        if (drawHandImg)
        {
            //_ Axis-aligned quad (fixed HAND_DEG); length spans the ring.
            float halfW  = (CyclicHandImageWidth * 0.5f) * zoomMult;
            float length = THICKNESS;

            ImVec2 base = ArcPoint(pos, RADIUS - THICKNESS * 0.5f,          HAND_DEG);
            ImVec2 tip  = ArcPoint(pos, RADIUS - THICKNESS * 0.5f + length, HAND_DEG);

            ImVec2 p0 = { base.x - halfW, base.y };
            ImVec2 p1 = { base.x + halfW, base.y };
            ImVec2 p2 = { tip.x  + halfW, tip.y  };
            ImVec2 p3 = { tip.x  - halfW, tip.y  };

            //_ UV origin at base - tip-up convention, same as an event icon.
            dl->AddImageQuad((ImTextureID)handImg->Resource, p0, p1, p2, p3,
                ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0), COL_HAND);
        }
        else
        {
            ImVec2 handTip  = ArcPoint(pos, RADIUS + THICKNESS * 0.5f, HAND_DEG);
            ImVec2 handBase = ArcPoint(pos, RADIUS - THICKNESS * 0.5f, HAND_DEG);
            dl->AddLine(handBase, handTip, COL_HAND, 2.0f);
        }

        //_ Decorative band, drawn last on top; future/past split like colTrack.
        if (drawRingImg)
        {
            float halfH = (CyclicRingImageThickness * 0.5f) * zoomMult;
            float offset = CyclicRingImageOffset * zoomMult;
            ImTextureID tex = (ImTextureID)ringImg->Resource;

            float futureSpan = ArcSpanDeg(ARC_FROM, HAND_DEG);
            float pastSpan   = ArcSpanDeg(HAND_DEG, ARC_TO);
            float totalSpan  = futureSpan + pastSpan;

            if (totalSpan > 0.0f)
            {
                float uSplit = futureSpan / totalSpan; //. U position of the HAND_DEG seam

                //_ Offset pushes each copy away, keeping the pair symmetric.
                float outerEdge = RADIUS + THICKNESS * 0.5f + offset;
                if (futureSpan > 0.0f)
                    DrawArcImage(dl, tex, pos, outerEdge, halfH, ARC_FROM, HAND_DEG,
                        false, 0.0f, uSplit);
                if (pastSpan > 0.0f)
                    DrawArcImage(dl, tex, pos, outerEdge, halfH, HAND_DEG, ARC_TO,
                        false, uSplit, 1.0f, 1.0f,
                        CyclicPastFadeEnabled ? 0.0f : 1.0f);

                float innerEdge = RADIUS - THICKNESS * 0.5f - offset;

                //_ Skips the inner copy once there's no room for it.
                if (innerEdge - halfH > 0.0f)
                {
                    //_ Mirrored V/U, not rotated (rotation breaks the fade).
                    if (futureSpan > 0.0f)
                        DrawArcImage(dl, tex, pos, innerEdge, halfH, ARC_FROM, HAND_DEG,
                            true, 1.0f, 1.0f - uSplit);
                    if (pastSpan > 0.0f)
                        DrawArcImage(dl, tex, pos, innerEdge, halfH, HAND_DEG, ARC_TO,
                            true, 1.0f - uSplit, 0.0f, 1.0f,
                            CyclicPastFadeEnabled ? 0.0f : 1.0f);
                }
            }
        }

        float hoverR = RADIUS + THICKNESS;

        //_ Drawn outside the ring's edge; shared with RenderMapEvents.
        if (isBeingEdited)
            DrawEditPulseRing(dl, pos, hoverR);

        bool hovered = ImGui::IsMouseHoveringRect(
            {pos.x - hoverR, pos.y - hoverR},
            {pos.x + hoverR, pos.y + hoverR});

        //_ Left-drag via a small anchor window; only "Stop" ends it.
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
            // One entry per unique slot NAME across the whole group, not per Slot struct -
            // two Slots sharing a name (e.g. Dry Top's two "Crash Site" slots at different
            // offsets) are one event as far as the user needs to know on hover. The ring
            // still draws every occurrence regardless; this collapsing is tooltip-text only.
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
                //_ A hidden slot shouldn't leak into the tooltip just because its arc is suppressed.
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
                        break; //. can't be active twice
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
                    //_ Reduces against whatever's already there for this name - active beats upcoming, otherwise soonest wins.
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
                //_ Same status swatches/threshold as the Basic Events map dots (maprender.cpp)
                if (e.active)
                {
                    ImGui::TextColored(ToImVec4(BasicEventColorActive),
                        "%s - Active (ends in %s)",
                        e.name.c_str(), FormatMinSec(e.secs).c_str());
                }
                else
                {
                    const float* col = e.secs < 900 ? BasicEventColorSoon
                                                      : BasicEventColorWaiting;
                    ImGui::TextColored(ToImVec4(col),
                        "%s - in %s",
                        e.name.c_str(), FormatCountdown(e.secs).c_str());
                }
            }

            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}