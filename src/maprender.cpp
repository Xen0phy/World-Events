#include "maprender.h"
#include "addon.h"
#include "events.h"
#include "settings.h"
#include "imgui.h"
#include <ctime>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// Icon textures — optional, per-event, user-supplied
// ---------------------------------------------------------------------------
// Mirrors the loading pattern already used for the Speedo's face/needle
// textures in another addon this project's author maintains: scan a
// "textures" folder under the addon's own directory, load on demand via
// Nexus's async Textures_LoadFromFile (never Textures_GetOrCreateFromFile,
// which can hand back a Texture_t* whose ->Resource is still null while
// the file decodes in the background, with no signal for when it becomes
// ready), and cache the result by filename.
//
// Unlike the Speedo (which has exactly one face texture and one needle
// texture), an arbitrary number of DIFFERENT events can each reference a
// different icon filename, so this needs a cache keyed by filename rather
// than a single static pointer.
//
// AUTHORING REQUIREMENT: because the icon is recolored at draw time via a
// multiplicative tint (ImDrawList::AddImage's `col` parameter multiplies
// every pixel's RGB/A by the tint color), the source image's RGB needs to
// already be a NEUTRAL GRAY (not necessarily pure white — any gray works,
// preserving relative shading/"shadows" within the icon) with the actual
// icon shape carried in the alpha channel. A full-color icon will tint
// unpredictably rather than cleanly recolor, since multiplying non-gray
// RGB by a tint shifts its hue instead of replacing it. This addon does
// NOT desaturate arbitrary images automatically — that would need a real
// image decoder (e.g. stb_image) to get at raw pixels before upload,
// which is more than this addon's scope calls for; the user is expected
// to prepare a gray/alpha icon themselves (e.g. desaturate + add a layer
// mask in any image editor) before dropping it in the textures folder.
// ---------------------------------------------------------------------------
struct EventIconEntry
{
    Texture_t*  texture     = nullptr;
    bool        requested   = false; // true once a load has been kicked off, even before it resolves
};

static std::unordered_map<std::string, EventIconEntry> s_iconCache;

static std::vector<std::string> s_iconFilenames;
static bool s_iconFilenamesScanned = false;

// Scan (or re-scan) "<addon dir>/textures" and rebuild s_iconFilenames.
// Call this to refresh after the user adds new files — there's no
// automatic filesystem-watching, matching the Speedo's existing pattern.
void ScanEventIconFiles()
{
    s_iconFilenames.clear();
    s_iconFilenamesScanned = true;

    std::string texDir = g_AddonDir + "\\textures";

    std::error_code ec;
    std::filesystem::create_directories(texDir, ec);

    for (auto& entry : std::filesystem::directory_iterator(texDir, ec))
    {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            s_iconFilenames.push_back(entry.path().filename().string());
    }
    std::sort(s_iconFilenames.begin(), s_iconFilenames.end());
}

const std::vector<std::string>& GetEventIconFilenames()
{
    if (!s_iconFilenamesScanned)
        ScanEventIconFiles();
    return s_iconFilenames;
}

static void OnEventIconReceived(const char* aIdentifier, Texture_t* aTexture)
{
    auto it = s_iconCache.find(aIdentifier);
    if (it != s_iconCache.end())
        it->second.texture = aTexture;
}

// Returns the loaded Texture_t* for this filename, or nullptr if it's not
// loaded yet (including: load not yet requested, in which case this also
// kicks off an async request — same fire-and-forget pattern as the
// Speedo's UpdateSpeedoTextures, just called per-filename on demand
// instead of once per frame for a couple of fixed slots).
static Texture_t* GetOrRequestEventIcon(const std::string& filename)
{
    if (filename.empty()) return nullptr;

    auto& entry = s_iconCache[filename]; // default-constructs on first use
    if (entry.texture && entry.texture->Resource)
        return entry.texture;

    if (!entry.requested)
    {
        entry.requested = true;
        std::string fullPath = g_AddonDir + "\\textures\\" + filename;
        // Identifier must be unique per filename so OnEventIconReceived
        // can route the callback back to the right cache entry — the
        // filename itself already is unique within this cache's key
        // space, so it doubles as the identifier directly.
        APIDefs->Textures_LoadFromFile(filename.c_str(), fullPath.c_str(), OnEventIconReceived);
    }

    return nullptr; // not ready yet this frame; falls back to the plain dot
}

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
// GetZoomPercent
// ---------------------------------------------------------------------------
// Returns how "zoomed in" the full-screen map currently is, as 0-100.
//
// Mumble's Compass.Scale is continent-units-per-pixel and gets SMALLER as
// you zoom in, but it has no fixed, documented min/max we can rely on as a
// constant (it depends on the player's screen resolution/UI scaling and,
// to a lesser extent, the current map). Rather than hardcode a guessed
// range, we track the smallest and largest Compass.Scale values we've
// actually observed and interpolate within that — i.e. the scaling
// calibrates itself the first time the user zooms fully in and fully out,
// and (since this is backed by BasicEventZoomScaleMinObserved/MaxObserved
// in settings_table.h, not a local static) stays calibrated across
// restarts too.
// ---------------------------------------------------------------------------
static float GetZoomPercent(float scale)
{
    if (scale <= 0.0f) return 0.0f;

    if (BasicEventZoomScaleMinObserved < 0.0f || scale < BasicEventZoomScaleMinObserved)
        BasicEventZoomScaleMinObserved = scale;
    if (BasicEventZoomScaleMaxObserved < 0.0f || scale > BasicEventZoomScaleMaxObserved)
        BasicEventZoomScaleMaxObserved = scale;

    float range = BasicEventZoomScaleMaxObserved - BasicEventZoomScaleMinObserved;
    if (range < 0.0001f) return 0.0f; // no zoom variation observed yet

    // scale is inverse to zoom: smallest scale == 100% zoomed in.
    float pct = (BasicEventZoomScaleMaxObserved - scale) / range * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

// ---------------------------------------------------------------------------
// GetEventZoomSizeMultiplier
// ---------------------------------------------------------------------------
// Markers stay at 1.0x from 0% zoom up to BasicEventZoomStartPct, then grow
// linearly to BasicEventZoomMaxMultiplier at 100% zoom. Declared in
// maprender.h (not static) so cyclicrender.cpp can reuse the exact same
// curve for cyclic group rings instead of duplicating this logic.
// ---------------------------------------------------------------------------
float GetEventZoomSizeMultiplier()
{
    float zoomPct = GetZoomPercent(MumbleLink->Context.Compass.Scale);

    if (!BasicEventZoomScalingEnabled) return 1.0f;

    float startPct = BasicEventZoomStartPct;
    if (startPct >= 100.0f) return 1.0f; // growth window degenerate; never grow
    if (zoomPct <= startPct) return 1.0f;

    float t = (zoomPct - startPct) / (100.0f - startPct); // 0..1 across the growth window
    if (t > 1.0f) t = 1.0f;

    return 1.0f + t * (BasicEventZoomMaxMultiplier - 1.0f);
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
// Draws a dot/icon for each event in g_Events. Size is normally fixed in
// pixels regardless of map zoom (zoom is already baked into Compass.Scale,
// which we only use to position the center) — but if BasicEventZoomScaling
// is enabled, markers additionally grow as the map is zoomed in past
// BasicEventZoomStartPct, up to BasicEventZoomMaxMultiplier at 100% zoom.
// See GetZoomPercent()/GetEventZoomSizeMultiplier() above.
// ---------------------------------------------------------------------------
void RenderMapEvents()
{
    // Background draw list, not foreground — see the comment in
    // cyclicrender.cpp's RenderCyclicGroups for why: the foreground list
    // composites AFTER tooltips, so the hover tooltip was rendering
    // underneath these dots. Background draws before all ImGui content
    // (including tooltips), fixing that, while still sitting on top of
    // GW2's own game-world rendering either way.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    constexpr float RING_THICK = 1.5f;
    constexpr ImU32 COL_RING   = IM_COL32(255, 255, 255, 220); // white ring

    time_t now = time(nullptr);

    // Same for every event this frame, so compute once rather than per-event.
    float zoomMult = GetEventZoomSizeMultiplier();

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

        bool active = IsActive(ev, now);
        int  secs   = SecondsUntilNext(ev, now);

        // Time-window filter: active events always show; upcoming events
        // only show if they start within the configured window. secs < 0
        // means "no timer data yet" — let those through unfiltered rather
        // than hiding events we simply don't have a countdown for.
        if (BasicEventTimeFilterEnabled && !active && secs >= 0 &&
            secs > BasicEventTimeFilterMinutes * 60)
            continue;

        // BasicEventColorActive/Soon/Waiting are user settings, stored as
        // packed RRGGBBAA (R in the top byte — same convention as
        // ColorSet::base in cyclic.h), NOT ImGui's native ABGR ImU32
        // packing, so they need the same explicit channel extraction
        // HEX()/ColorSet uses rather than being passed to IM_COL32-style
        // calls directly. There's no separate hardcoded alpha layered on
        // top any more — whatever alpha the user picked via the color
        // swatch (the LSB of the packed value) IS the actual opacity
        // used, for both the plain dot and the icon tint alike.
        auto toImU32 = [](unsigned int rrggbbaa) {
            return IM_COL32(
                (rrggbbaa >> 24) & 0xFF,  // R
                (rrggbbaa >> 16) & 0xFF,  // G
                (rrggbbaa >>  8) & 0xFF,  // B
                  rrggbbaa        & 0xFF  // A
            );
        };

        ImU32 colFill = active                    ? toImU32(BasicEventColorActive)
                      : (secs >= 0 && secs < 900) ? toImU32(BasicEventColorSoon)
                      :                              toImU32(BasicEventColorWaiting);

        Texture_t* icon = ev.iconTexture.empty() ? nullptr : GetOrRequestEventIcon(ev.iconTexture);

        // hoverHalfExtent tracks whichever size is ACTUALLY drawn for
        // this specific event, so the hover/tooltip rect matches what's
        // visually on screen — an icon-using event and a plain-dot event
        // can now have different effective sizes (BasicEventIconSize vs
        // BasicEventDotRadius), so a single shared hover radius would be
        // wrong for whichever one isn't currently active.
        float hoverHalfExtent;

        if (icon && icon->Resource)
        {
            float halfW = BasicEventIconSize * zoomMult;
            float halfH = halfW * ((float)icon->Height / (float)icon->Width);
            dl->AddImage((ImTextureID)icon->Resource,
                ImVec2(pos.x - halfW, pos.y - halfH),
                ImVec2(pos.x + halfW, pos.y + halfH),
                ImVec2(0, 0), ImVec2(1, 1), colFill);
            hoverHalfExtent = halfW; // width is usually the dominant dimension for icon art; close enough for a hover box
        }
        else
        {
            float radius = BasicEventDotRadius * zoomMult;
            dl->AddCircleFilled(pos, radius, colFill);
            dl->AddCircle(pos, radius, COL_RING, 0, RING_THICK);
            hoverHalfExtent = radius;
        }

        // Tooltip on hover
        if (ImGui::IsMouseHoveringRect(
                {pos.x - hoverHalfExtent, pos.y - hoverHalfExtent},
                {pos.x + hoverHalfExtent, pos.y + hoverHalfExtent}))
        {
            int secs = SecondsUntilNext(ev, now);
            if (secs < 0) continue; // skip events with no timer data yet

            ImGui::BeginTooltip();
            if (active)
            {
                int secsUntilEnd = SecondsUntilEnd(ev, now);
                ImGui::Text("%s — Active (ends in %dm %02ds)",
                    ev.name.c_str(), secsUntilEnd / 60, secsUntilEnd % 60);
            }
            else if (secs >= 3600)
            {
                ImGui::Text("%s — in %dh %02dm",
                    ev.name.c_str(), secs / 3600, (secs % 3600) / 60);
            }
            else
            {
                ImGui::Text("%s — in %dm %02ds",
                    ev.name.c_str(), secs / 60, secs % 60);
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::End();
}