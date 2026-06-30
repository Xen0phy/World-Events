#include "cyclicrender.h"
#include "addon.h"
#include "cyclic.h"
#include "maprender.h"
#include "settings.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <ctime>
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <string>

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
//   Hand fixed at 0° (top, "now"). Events rotate counter-clockwise.
//   Arc covers 270° total (ARC_FROM/ARC_TO), leaving a 90° gap centered
//   at the bottom of the circle, opposite the hand.
//
//   1° = SECS_PER_DEG seconds (26.67s for a 2h cycle).
//
//   Future events sit CW from the hand (positive angles from 0°, growing
//   toward 270°). Entry point ARC_FROM=270° ≈ 75min in the future
//   (MAX_FUTURE_SECS).
//   Past events sit CCW from the hand (angles approaching 360°, capped at
//   ARC_TO=270°). Exit point ≈ 15min in the past (MAX_PAST_SECS).
//
//   The past portion of the track (0° to 270°, measured CCW from the hand)
//   fades from full opacity at the hand to transparent at the exit point.
//
//   Each occurrence of a slot is drawn from TWO INDEPENDENT clocks:
//     - EXIT: its own active/past fade-out, computed per wrap-pass (a
//       wrapping occurrence is split into a this-cycle tail + next-cycle
//       head so the active segment renders correctly across the 0s/period
//       boundary).
//     - ENTRY: the lead-in to this occurrence's NEXT recurrence, computed
//       once per occurrence — using the occurrence's true offset/duration,
//       not a wrap-pass's partial duration — from (period - phase), i.e.
//       time until that same occurrence starts again. This is tracked
//       independently of whether the current occurrence is still active,
//       which is what lets the entering arc appear before the exiting arc
//       has fully faded out, instead of waiting for it to disappear first.
// ---------------------------------------------------------------------------

static constexpr float HAND_DEG      = 0.0f;    // top   — now

// NOTE: SECS_PER_DEG used to be a single file-scope constexpr hardcoded to
// a 7200s cycle. It's now computed per-group inside RenderCyclicGroups
// from that group's own `period`, since groups can have different cycle
// lengths (Dry Top runs 3600s; most others run 7200s) — a single hardcoded
// value silently misdrew any group whose period didn't match it.
//
// MAX_FUTURE_SECS / MAX_PAST_SECS are likewise computed per-group now, from
// settings stored as DEGREES (CyclicMaxFutureDeg / CyclicMaxPastDeg, see
// settings_table.h) so the user can adjust the entry/exit window at runtime
// via the options panel, independent of any one group's period. ARC_FROM/
// ARC_TO are always exactly those same degree settings — they're derived,
// not independently stored, so the two can never silently disagree.

void RenderCyclicGroups()
{
    // Background draw list, not foreground: the foreground list is
    // composited LAST among all ImGui content, which includes tooltips
    // (a tooltip is just another ImGui window under the hood) — so rings
    // drawn on the foreground list always painted over the hover tooltip,
    // regardless of draw call order within this function. The background
    // list draws FIRST among ImGui content, before every regular window
    // and tooltip, so the tooltip now correctly composites on top.
    // This only affects ordering relative to other ImGui content — GW2's
    // own game-world rendering happens in an entirely separate pass below
    // all ImGui content either way, so the rings still sit on top of the
    // game itself exactly as before.
    ImDrawList* dl  = ImGui::GetBackgroundDrawList();
    time_t      now = time(nullptr);

    const float zoomMult  = GetEventZoomSizeMultiplier();
    const float RADIUS    = CyclicRadius    * zoomMult;
    const float THICKNESS = CyclicThickness * zoomMult;
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
        // SECS_PER_DEG is derived from THIS group's own period, not a
        // hardcoded constant — Dry Top runs a 3600s cycle while most other
        // groups run 7200s, so a single global SECS_PER_DEG would silently
        // misdraw every group whose period differs from whatever value
        // happened to be hardcoded.
        const float SECS_PER_DEG = (float)grp.period / 360.0f;

        // CyclicMaxFutureDeg/CyclicMaxPastDeg are stored as DEGREES
        // (period-independent) and converted to this group's own seconds
        // here, at the point of use — see settings_table.h for why seconds
        // can't be the stored unit when groups have different periods.
        const float MAX_FUTURE_SECS = CyclicMaxFutureDeg * SECS_PER_DEG;
        const float MAX_PAST_SECS   = CyclicMaxPastDeg   * SECS_PER_DEG;

        // ARC_FROM is the future-side boundary, measured directly from the hand (0°).
        // ARC_TO is the past-side boundary for the BACKGROUND TRACK specifically:
        // the per-slot past-fade (below) sweeps from the hand (0°) up toward 360°,
        // so its boundary is expressed as (360° - past-angle), not the past-angle
        // itself — these are NOT the same number unless future-angle + past-angle
        // happens to equal 360° (true by coincidence with the old hardcoded
        // 270°/90° values, which is why this distinction was easy to miss).
        const float ARC_FROM = CyclicMaxFutureDeg;
        const float ARC_TO   = 360.0f - CyclicMaxPastDeg;

        ImVec2 pos = ContinentToScreen(grp.continentX, grp.continentY);

        if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
        if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;

        // --- Background track, idle-colored (group's idleColor, defaulting
        // to colors.ter() — see CyclicGroup::IdleColor() in cyclic.h) ---
        // RGB comes from the idle color; alpha comes from COL_TRACK, so the
        // idle background stays visually translucent/recessed compared to
        // an active slot's full-opacity arc, regardless of which color
        // ends up being used as the idle color.
        ImU32 idle     = grp.IdleColor();
        ImU32 colTrack = (idle & 0x00FFFFFF) | (COL_TRACK & 0xFF000000);

        // Future portion: solid
        DrawArc(dl, pos, RADIUS, ARC_FROM, HAND_DEG, colTrack, THICKNESS);
        // Past portion: fades from full opacity at hand to transparent at exit
        DrawArc(dl, pos, RADIUS, HAND_DEG, ARC_TO,   colTrack, THICKNESS,
                1.0f, 0.0f);

        // --- Draw each slot ---
        int secondsOfDay = (int)(now % 86400);

        for (const auto& slot : grp.slots)
        {
            ImU32 color  = grp.SlotColor(slot);
            int repeat   = slot.repeat > 0 ? slot.repeat : 1;
            int subSpan  = grp.period / repeat; // spacing between repeated occurrences

            for (int r = 0; r < repeat; r++)
            {
                int baseOffset = slot.offset + r * subSpan;

                // --- Exit side: split wrapping occurrences into two passes so the
                // active/fade-out segment renders correctly across the 0s/period
                // boundary. (Tail of this cycle + head of next.)
                //
                // BUGFIX: pass 0's duration must be the slot's own duration
                // (capped at what actually fits before the period boundary),
                // NOT (period - baseOffset) — that older formula accidentally
                // used "time remaining in the cycle" as the active-window
                // length, which is only correct by coincidence when duration
                // happens to reach exactly to the period boundary. For any
                // slot/occurrence starting at offset 0 (e.g. "Mordremoth
                // Progress", or repeat-occurrence r=0 of "Forged with Fire"),
                // this made `active` true for almost the entire cycle.
                int slotEnd = baseOffset + slot.duration;
                int wrapDur = slotEnd > grp.period ? slotEnd % grp.period : 0;
                int passes  = wrapDur ? 2 : 1;

                for (int pass = 0; pass < passes; pass++)
                {
                    int offset   = pass == 0 ? baseOffset : 0;
                    int duration = pass == 0 ? (slot.duration - wrapDur) : wrapDur;
                    if (duration <= 0) continue;

                    int phase         = ((secondsOfDay - offset) % grp.period + grp.period) % grp.period;
                    bool active       = (phase < duration);
                    int secsFromStart = phase;

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
                                    color, THICKNESS);

                        // Past part: fades to transparent
                        if (pastDeg < 360.0f)
                            DrawArc(dl, pos, RADIUS, HAND_DEG, pastDeg,
                                    color, THICKNESS, 1.0f, 0.0f);
                    }
                }

                // --- Entry side: lead-in of THIS occurrence's next recurrence.
                // Computed ONCE per occurrence (not per wrap-pass) from the
                // occurrence's true offset/duration — a wrapping occurrence is
                // one logical event, so it must only produce one entry arc for
                // its next recurrence, not one per pass.
                //
                // Time until next start is independent of whether the current
                // occurrence is still active — that's what lets the entering arc
                // appear before the exiting arc (above) has fully faded out,
                // instead of waiting for `active` to flip.
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
            ImGui::TextUnformatted(grp.name.c_str());
            ImGui::Separator();

            // Collect one status entry PER UNIQUE SLOT NAME across the whole
            // group — not one per Slot entry. Two Slot entries sharing a
            // name (whether from one Slot's own `repeat` occurrences, or
            // from separate Slot entries that happen to share a name, e.g.
            // Dry Top's two "Crash Site" slots at different offsets) are
            // the same event as far as the user needs to know on hover:
            // only the active occurrence (if any) or the single soonest
            // upcoming occurrence matters. The ring still draws every
            // occurrence regardless — this collapsing is tooltip-text only.
            struct TooltipEntry
            {
                std::string name;
                ImU32       color;
                bool        active;
                int         secs;   // secsLeft if active, secsUntilStart if upcoming
            };

            // Keyed by name so multiple Slot entries sharing a name reduce
            // to a single candidate before anything gets pushed into the
            // final, sorted entries list below.
            std::unordered_map<std::string, TooltipEntry> byName;

            for (const auto& slot : grp.slots)
            {
                ImU32 color = grp.SlotColor(slot);
                int repeat  = slot.repeat > 0 ? slot.repeat : 1;
                int subSpan = grp.period / repeat;

                bool foundActive    = false;
                int  activeSecsLeft = 0;
                bool foundUpcoming  = false;
                int  bestSecsUntil  = grp.period;

                for (int r = 0; r < repeat; r++)
                {
                    int baseOffset = slot.offset + r * subSpan;
                    int phase          = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
                    bool active        = (phase < slot.duration);
                    int secsUntilStart = active ? 0 : (grp.period - phase);

                    if (active)
                    {
                        foundActive    = true;
                        activeSecsLeft = slot.duration - phase;
                        break; // a slot can't be active in two repeats at once
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
                    ? TooltipEntry{ slot.name, color, true, activeSecsLeft }
                    : TooltipEntry{ slot.name, color, false, bestSecsUntil };

                auto it = byName.find(slot.name);
                if (it == byName.end())
                {
                    byName.emplace(slot.name, candidate);
                }
                else
                {
                    // Reduce against whatever's already there for this name:
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
                if (a.active != b.active) return a.active; // active entries first
                return a.secs < b.secs;                    // then soonest first
            });

            for (const auto& e : entries)
            {
                if (e.active)
                {
                    ImGui::TextColored(ImVec4(
                        ((e.color >>  0) & 0xFF) / 255.0f,
                        ((e.color >>  8) & 0xFF) / 255.0f,
                        ((e.color >> 16) & 0xFF) / 255.0f, 1.0f),
                        "%s — Active (ends in %dm %02ds)",
                        e.name.c_str(), e.secs / 60, e.secs % 60);
                }
                else
                {
                    int s = e.secs;
                    if (s >= 3600)
                        ImGui::Text("%s — in %dh %02dm",
                            e.name.c_str(), s / 3600, (s % 3600) / 60);
                    else
                        ImGui::Text("%s — in %dm %02ds",
                            e.name.c_str(), s / 60, s % 60);
                }
            }

            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}
