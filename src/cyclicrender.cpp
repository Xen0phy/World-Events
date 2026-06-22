#include "cyclicrender.h"
#include "addon.h"
#include "cyclic.h"
#include "maprender.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <ctime>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// ---------------------------------------------------------------------------
// ArcPoint
// ---------------------------------------------------------------------------
// Returns a point on a circle of given radius, at angle_deg degrees.
// 0° = top (12 o'clock), increases clockwise.
// ---------------------------------------------------------------------------
static ImVec2 ArcPoint(ImVec2 center, float radius, float angle_deg)
{
    float rad = (float)((angle_deg - 90.0) * M_PI / 180.0);
    return {
        center.x + radius * cosf(rad),
        center.y + radius * sinf(rad)
    };
}

// ---------------------------------------------------------------------------
// DrawArc
// ---------------------------------------------------------------------------
// Draws a thick arc segment counter-clockwise from from_deg to to_deg.
// Supports per-vertex alpha fade — pass alphaFrom=alphaTo for solid color.
// Uses the low-level PrimVtx API so the fade is gapless.
// ---------------------------------------------------------------------------
static void DrawArc(ImDrawList* dl, ImVec2 center, float radius,
    float from_deg, float to_deg,
    ImU32 color, float thickness,
    float alphaFrom = 1.0f, float alphaTo = 1.0f)
{
    float span = from_deg - to_deg;
    if (span <= 0) span += 360.0f;
    if (span <= 0) return;

    int   steps = (int)ceilf(span);
    float step  = span / (float)steps;
    float half  = thickness * 0.5f;
    ImU32 rgb   = color & 0x00FFFFFF;

    // Reserve: each segment is a quad = 4 vertices, 6 indices
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

        // Two triangles forming a quad, colors interpolated across vertices
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

// ---------------------------------------------------------------------------
// RenderCyclicGroups
// ---------------------------------------------------------------------------
// Draws a 270° arc for each CyclicGroup. Arc geometry:
//
//   Hand fixed at 0° (top). Events rotate counter-clockwise.
//   Arc drawn CCW from 225° (entry, bottom-left) to 315° (exit, upper-left).
//   Gap: 315° → 225° CCW = 90° gap on the left side.
//
//   1° = SECS_PER_DEG seconds (26.67s for a 2h cycle).
//
//   Future events sit CW from the hand (positive angles from 0°).
//     Entry point 225° = 225 * 26.67s ≈ 100min in the future.
//   Past events sit CCW from the hand (angles near 360°).
//     Exit point 315° = 45 * 26.67s ≈ 20min in the past.
//
//   The past portion of the track (0° to 315° CCW) fades from full
//   opacity at the hand to transparent at the exit point.
// ---------------------------------------------------------------------------

static constexpr float SECS_PER_DEG  = 7200.0f / 360.0f;
static constexpr float ARC_FROM      = 270.0f;  // entry — furthest future
static constexpr float ARC_TO        = 270.0f;  // exit  — furthest past
static constexpr float HAND_DEG      = 0.0f;    // top   — now

static constexpr float MAX_FUTURE_SECS = 270.0f * SECS_PER_DEG; // ~75min
static constexpr float MAX_PAST_SECS   =  90.0f * SECS_PER_DEG; // ~15min

void RenderCyclicGroups()
{
    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    time_t      now = time(nullptr);

    constexpr float RADIUS    = 20.0f;
    constexpr float THICKNESS = 40.0f;
    constexpr ImU32 COL_TRACK = IM_COL32(100, 100, 100, 120);
    constexpr ImU32 COL_HAND  = IM_COL32(255, 255, 255, 240);

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##we_cyclic_overlay", nullptr,
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoInputs        |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    for (const auto& grp : g_CyclicGroups)
    {
        ImVec2 pos = ContinentToScreen(grp.continentX, grp.continentY);

        if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
        if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;

        // --- Gray background track ---
        // Future portion: solid
        DrawArc(dl, pos, RADIUS, ARC_FROM, HAND_DEG, COL_TRACK, THICKNESS);
        // Past portion: fades from full opacity at hand to transparent at exit
        DrawArc(dl, pos, RADIUS, HAND_DEG, ARC_TO,   COL_TRACK, THICKNESS,
                1.0f, 0.0f);

        // --- Draw each slot ---
        int secondsOfDay = (int)(now % 86400);

        for (const auto& slot : grp.slots)
        {
            // Split wrapping slots into two passes: tail of this cycle + head of next.
            int slotEnd  = slot.offset + slot.duration;
            int wrapDur  = slotEnd > grp.period ? slotEnd % grp.period : 0;
            int passes   = wrapDur ? 2 : 1;

            for (int pass = 0; pass < passes; pass++)
            {
                int offset   = pass == 0 ? slot.offset : 0;
                int duration = pass == 0 ? (grp.period - slot.offset) : wrapDur;

                // Phase within this pass
                int phase          = ((secondsOfDay - offset) % grp.period + grp.period) % grp.period;
                bool active        = (phase < duration);
                int secsFromStart  = phase;
                int secsUntilStart = active ? 0 : (grp.period - phase);

                if (active)
                {
                    // Segment straddles the hand.
                    float futureDeg = fminf((float)(duration - secsFromStart) / SECS_PER_DEG,
                                            ARC_FROM);
                    float pastSecs  = fminf((float)secsFromStart, MAX_PAST_SECS);
                    float pastDeg   = 360.0f - pastSecs / SECS_PER_DEG;

                    // Future part: solid color
                    if (futureDeg > HAND_DEG)
                        DrawArc(dl, pos, RADIUS, futureDeg, HAND_DEG,
                                slot.color, THICKNESS);

                    // Past part: fades to transparent
                    if (pastDeg < 360.0f)
                        DrawArc(dl, pos, RADIUS, HAND_DEG, pastDeg,
                                slot.color, THICKNESS, 1.0f, 0.0f);
                }
                else
                {
                    // Segment fully in the future — skip if beyond entry point
                    if ((float)secsUntilStart > MAX_FUTURE_SECS) continue;

                    float leadDeg  = (float)secsUntilStart / SECS_PER_DEG;
                    float trailDeg = fminf((float)(secsUntilStart + duration) / SECS_PER_DEG,
                                           ARC_FROM);

                    DrawArc(dl, pos, RADIUS, trailDeg, leadDeg, slot.color, THICKNESS);
                }
            }
        }

        // --- Fixed hand at top (0°) ---
        ImVec2 handTip  = ArcPoint(pos, RADIUS + THICKNESS * 0.5f, HAND_DEG);
        ImVec2 handBase = ArcPoint(pos, RADIUS - THICKNESS * 0.5f, HAND_DEG);
        dl->AddLine(handBase, handTip, COL_HAND, 2.0f);

        // --- Tooltip on hover ---
        float hoverR = RADIUS + THICKNESS;
        if (ImGui::IsMouseHoveringRect(
                {pos.x - hoverR, pos.y - hoverR},
                {pos.x + hoverR, pos.y + hoverR}))
        {
            ImGui::BeginTooltip();

            for (const auto& slot : grp.slots)
            {
                // Fixed phase formula — same as draw loop
                int phase          = ((secondsOfDay - slot.offset) % grp.period + grp.period) % grp.period;
                bool active        = (phase < slot.duration);
                int secsFromStart  = phase;
                int secsUntilStart = active ? 0 : (grp.period - phase);

                if (active)
                {
                    int secsLeft = slot.duration - secsFromStart;
                    ImGui::TextColored(ImVec4(
                        ((slot.color >>  0) & 0xFF) / 255.0f,
                        ((slot.color >>  8) & 0xFF) / 255.0f,
                        ((slot.color >> 16) & 0xFF) / 255.0f, 1.0f),
                        "%s — Active (ends in %dm %02ds)",
                        slot.name, secsLeft / 60, secsLeft % 60);
                }
                else if ((float)secsUntilStart <= MAX_FUTURE_SECS)
                {
                    int s = secsUntilStart;
                    if (s >= 3600)
                        ImGui::Text("%s — in %dh %02dm",
                            slot.name, s / 3600, (s % 3600) / 60);
                    else
                        ImGui::Text("%s — in %dm %02ds",
                            slot.name, s / 60, s % 60);
                }
            }

            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}
