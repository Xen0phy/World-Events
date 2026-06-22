#include "maprender.h"
#include "addon.h"
#include "events.h"
#include "imgui.h"
#include <ctime>

// ---------------------------------------------------------------------------
// SecondsUntilNext
// ---------------------------------------------------------------------------
// Returns how many seconds until the next occurrence of an event.
//
// For varying events, walks the list in order:
//   - if the index hasn't started yet  → return time until it starts
//   - if it's within the active window → return 0 (active now)
//   - if all times passed today        → wrap to first one tomorrow
//
// For periodic events, uses phase arithmetic to find the next start.
// ---------------------------------------------------------------------------
static int SecondsUntilNext(const WorldEvent& ev, time_t now)
{
    if (!ev.isVarying && ev.period <= 0) return -1;
    if ( ev.isVarying && ev.varyingTimes.empty()) return -1;

    int secondsOfDay = (int)(now % 86400);

    if (ev.isVarying)
    {
        for (int t : ev.varyingTimes)
        {
            if (secondsOfDay < t)
                return t - secondsOfDay;            // hasn't started yet
            if (secondsOfDay < t + ev.duration)
                return 0;                           // active right now
            // else: already passed, check next index
        }
        // All times passed today, wrap to first one tomorrow.
        return 86400 - secondsOfDay + ev.varyingTimes[0];
    }

    // Periodic: phase = how far into the current cycle we are
    int phase = ((secondsOfDay - ev.offset) % ev.period + ev.period) % ev.period;
    return ev.period - phase;
}

// ---------------------------------------------------------------------------
// SecondsUntilEnd
// ---------------------------------------------------------------------------
// Returns how many seconds until the current active window closes.
// Only meaningful when IsActive() is true.
// ---------------------------------------------------------------------------
static int SecondsUntilEnd(const WorldEvent& ev, time_t now)
{
    int secondsOfDay = (int)(now % 86400);

    if (ev.isVarying)
    {
        for (int t : ev.varyingTimes)
            if (secondsOfDay >= t && secondsOfDay < t + ev.duration)
                return t + ev.duration - secondsOfDay;
        return 0;
    }

    // Periodic: phase is how far into the cycle we are, duration - phase = time left
    int phase = ((secondsOfDay - ev.offset) % ev.period + ev.period) % ev.period;
    return ev.duration - phase;
}

// ---------------------------------------------------------------------------
// IsActive
// ---------------------------------------------------------------------------
// Returns true if the event is currently running.
//
// For varying events, checks directly whether now falls inside any active
// window. For periodic events, checks whether we are within the duration
// window at the start of the current cycle.
// ---------------------------------------------------------------------------
static bool IsActive(const WorldEvent& ev, time_t now)
{
    if (ev.isVarying)
    {
        if (ev.varyingTimes.empty()) return false;
        int secondsOfDay = (int)(now % 86400);
        for (int t : ev.varyingTimes)
            if (secondsOfDay >= t && secondsOfDay < t + ev.duration)
                return true;
        return false;
    }

    int secs = SecondsUntilNext(ev, now);
    if (secs < 0) return false;
    return secs > (ev.period - ev.duration);
}

// ---------------------------------------------------------------------------
// ContinentToScreen
// ---------------------------------------------------------------------------
// Maps a GW2 continent coordinate (cx, cy) to a screen pixel position.
//
// The Mumble compass gives us everything we need:
//   Compass.Center — the continent coordinate at the CENTER of the map view
//   Compass.Scale  — continent units per pixel (decreases as you zoom in)
//
// The center of the map on screen is approximately the center of the window.
// NOTE: This may need a small offset tuning once tested in-game, as GW2's
// full-screen map has a thin UI border. We'll adjust that here if needed.
// ---------------------------------------------------------------------------
ImVec2 ContinentToScreen(float cx, float cy)
{
    const auto& compass = MumbleLink->Context.Compass;

    // Screen position that Compass.Center maps to.
    // For the full-screen map this is the window center.
    float screenCX = NexusLink->Width  * 0.5f;
    float screenCY = NexusLink->Height * 0.5f;

    float scale = compass.Scale / NexusLink->Scaling;
    if (scale < 0.0001f) scale = 1.0f; // guard against divide-by-zero on init

    return {
        screenCX + (cx - compass.Center.X) / scale,
        screenCY + (cy - compass.Center.Y) / scale
    };
}

// ---------------------------------------------------------------------------
// RenderMapEvents
// ---------------------------------------------------------------------------
// Draws a fixed-size dot for each event in g_Events.
// The dot stays the same pixel size regardless of map zoom because the
// zoom is already baked into Compass.Scale — we only use it to position
// the center, then draw at a hardcoded pixel radius.
// ---------------------------------------------------------------------------
void RenderMapEvents()
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    constexpr float RADIUS     = 8.0f;
    constexpr float RING_THICK = 1.5f;
    constexpr ImU32 COL_RING   = IM_COL32(255, 255, 255, 220); // white ring

    time_t now = time(nullptr);

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##we_overlay", nullptr,
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoInputs        |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    for (const auto& ev : g_Events)
    {
        ImVec2 pos = ContinentToScreen(ev.continentX, ev.continentY);

        // Cull anything off-screen so we don't draw into infinite space
        // if the map center is far from any event.
        if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
        if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;

        bool  active  = IsActive(ev, now);
        int   secs    = SecondsUntilNext(ev, now);
        ImU32 colFill = active                        ? IM_COL32(255,  50,  50, 180)  // red    — active
                      : (secs >= 0 && secs < 900)     ? IM_COL32(255, 140,   0, 180)  // orange — soon
                      :                                 IM_COL32(160, 160, 160, 180); // gray   — waiting

        dl->AddCircleFilled(pos, RADIUS, colFill);
        dl->AddCircle(pos, RADIUS, COL_RING, 0, RING_THICK);

        // Tooltip on hover
        if (ImGui::IsMouseHoveringRect(
                {pos.x - RADIUS, pos.y - RADIUS},
                {pos.x + RADIUS, pos.y + RADIUS}))
        {
            int secs = SecondsUntilNext(ev, now);
            if (secs < 0) continue; // skip events with no timer data yet

            ImGui::BeginTooltip();
            if (active)
            {
                int secsUntilEnd = SecondsUntilEnd(ev, now);
                ImGui::Text("%s — Active (ends in %dm %02ds)",
                    ev.name, secsUntilEnd / 60, secsUntilEnd % 60);
            }
            else if (secs >= 3600)
            {
                ImGui::Text("%s — in %dh %02dm",
                    ev.name, secs / 3600, (secs % 3600) / 60);
            }
            else
            {
                ImGui::Text("%s — in %dm %02ds",
                    ev.name, secs / 60, secs % 60);
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}