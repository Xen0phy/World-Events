//################################################################################
// maprender.cpp
//--------------------------------------------------------------------------------
// EventIconEntry/s_iconCache   per-filename cache of loaded icon textures
// GetOrRequestEventIcon         returns/kicks off the load for one icon
//                               (public - also used by cyclicrender.cpp's
//                               ring-image overlay, see maprender.h)
// GetSecondsUntilEventStart/GetSecondsUntilEventEnd/IsEventActive
//                               Basic Event schedule queries (see maprender.h)
// GetEventZoomSizeMultiplier   current zoom-based marker size multiplier
// ContinentToScreen/ScreenToContinent
//                               continent coordinate <-> screen pixel
// g_EditMode/ClearEditMode     shared drag-to-reposition state
// RenderMapEvents               draws all Basic Events + live-event radius
//                               rings onto the open world map
//--------------------------------------------------------------------------------
// Icon textures are optional, per-event, and either user-supplied or bundled
// default. Scans a "textures" folder under the addon's own directory and loads on
// demand via Nexus's async Textures_LoadFromFile - never
// Textures_GetOrCreateFromFile, which can return a Texture_t* whose ->Resource is
// still null while the file decodes in the background, with no ready signal.
// Results are cached by filename, since multiple events can share an icon.
//
// A small set of default icons is compiled into the dll (see bundled_icons.h /
// g_BundledIcons) and loaded via Textures_LoadFromMemory instead of
// LoadFromFile, so no files need to exist on disk out of the box. Disk is checked
// first for a given filename (see GetOrRequestEventIcon), so a user can override
// a bundled icon by dropping a same-named file into their own textures/ folder.
//
// AUTHORING REQUIREMENT: the icon is recolored at draw time via a multiplicative
// tint (ImDrawList::AddImage's `col` parameter multiplies each pixel's RGB/A by
// the tint color), so the source image's RGB must be a NEUTRAL GRAY (any gray
// works, preserving relative shading) with the icon shape carried in the alpha
// channel. A full-color icon shifts hue instead of recoloring cleanly. This addon
// does not desaturate images automatically; users must prepare a gray/alpha icon
// themselves before adding it to the textures folder.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "bundled_icons.h"
#include "color_utils.h"
#include "events.h"
#include "events_live.h"
#include "imgui.h"
#include "map_shared.h"
#include "maprender.h"
#include "settings.h"
#include "time_format.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//********************************************************************************
// EventIconEntry
//--------------------------------------------------------------------------------
// texture     the loaded texture, once ready
// requested   true once a load has been kicked off, even before it resolves
//--------------------------------------------------------------------------------
struct EventIconEntry
{
    Texture_t*  texture     = nullptr;
    bool        requested   = false;
};

//_ Guards s_iconCache: loaded on a background thread, read on the render thread.
static std::mutex s_iconCacheMutex;
static std::unordered_map<std::string, EventIconEntry> s_iconCache;

static std::vector<std::string> s_iconFilenames;
static bool s_iconFilenamesScanned = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FindDefaultIcon
//--------------------------------------------------------------------------------
// Searches g_BundledIcons (bundled_icons.h), the generated table of every PNG
// under resources/textures/.
//--------------------------------------------------------------------------------
static const DefaultIconEntry* FindDefaultIcon(const std::string& filename)
{
    for (size_t i = 0; i < g_BundledIconsCount; i++)
        if (filename == g_BundledIcons[i].name)
            return &g_BundledIcons[i];
    return nullptr;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScanEventIconFiles   (pairs with: GetEventIconFilenames)
//--------------------------------------------------------------------------------
// Scan (or re-scan) "<addon dir>/textures" and rebuild s_iconFilenames. Call this
// to refresh after the user adds new files - there's no automatic filesystem-
// watching.
//
// Also merges in the bundled default icon names, see GetOrRequestEventIcon.
//--------------------------------------------------------------------------------
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

    for (size_t i = 0; i < g_BundledIconsCount; i++)
        if (std::find(s_iconFilenames.begin(), s_iconFilenames.end(), g_BundledIcons[i].name) == s_iconFilenames.end())
            s_iconFilenames.push_back(g_BundledIcons[i].name);

    std::sort(s_iconFilenames.begin(), s_iconFilenames.end());
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetEventIconFilenames   (pairs with: ScanEventIconFiles)
//--------------------------------------------------------------------------------
const std::vector<std::string>& GetEventIconFilenames()
{
    if (!s_iconFilenamesScanned)
        ScanEventIconFiles();
    return s_iconFilenames;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OnEventIconReceived
//--------------------------------------------------------------------------------
static void OnEventIconReceived(const char* aIdentifier, Texture_t* aTexture)
{
    std::lock_guard<std::mutex> lock(s_iconCacheMutex);
    auto it = s_iconCache.find(aIdentifier);
    if (it != s_iconCache.end())
        it->second.texture = aTexture;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetOrRequestEventIcon
//--------------------------------------------------------------------------------
// Returns the loaded Texture_t* for this filename, or nullptr if it's not loaded
// yet (including: load not yet requested, in which case this also kicks off an
// async request).
//
// DISK FIRST: if a file by this name exists under <addonDir>/textures, it's
// loaded from there exactly as before, even if the same name also exists in
// g_BundledIcons - this is what lets a user override/reskin a bundled icon just
// by dropping a same-named file, no rebuild needed. Only when nothing matches on
// disk does this fall back to the bundled table via Textures_LoadFromMemory
// instead of Textures_LoadFromFile.
//--------------------------------------------------------------------------------
Texture_t* GetOrRequestEventIcon(const std::string& filename)
{
    if (filename.empty()) return nullptr;

    //_ Only the lookup/insert needs the lock; dispatch below runs outside it.
    bool needsRequest = false;
    {
        std::lock_guard<std::mutex> lock(s_iconCacheMutex);

        auto& entry = s_iconCache[filename]; //. default-constructs on first use
        if (entry.texture && entry.texture->Resource)
            return entry.texture;

        if (!entry.requested)
        {
            entry.requested = true;
            needsRequest = true;
        }
    }

    if (needsRequest)
    {
        std::string fullPath = g_AddonDir + "\\textures\\" + filename;

        //_ Identifier must be unique so OnEventIconReceived can route it back.
        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec))
        {
            APIDefs->Textures_LoadFromFile(filename.c_str(), fullPath.c_str(), OnEventIconReceived);
        }
        else if (const DefaultIconEntry* bundled = FindDefaultIcon(filename))
        {
            //_ aData is non-const in the Nexus signature though this only reads it.
            APIDefs->Textures_LoadFromMemory(filename.c_str(), (void*)bundled->data, bundled->size, OnEventIconReceived);
        }
    }

    return nullptr; //. not ready yet this frame
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetSecondsUntilEventStart   (group: GetSecondsUntilEventEnd, IsEventActive)
//--------------------------------------------------------------------------------
// Returns how many seconds until the next occurrence of an event.
//
// For varying events, walks the list in order:
//   - if the index hasn't started yet  -> return time until it starts
//   - if it's within the active window -> return 0 (active now)
//   - if all times passed today        -> wrap to first one tomorrow
//
// For periodic events, uses phase arithmetic to find the next start.
//--------------------------------------------------------------------------------
int GetSecondsUntilEventStart(const WorldEvent& ev, time_t now)
{
    if (!ev.isVarying && ev.period <= 0) return -1;
    if ( ev.isVarying && ev.varyingTimes.empty()) return -1;

    int secondsOfDay = (int)(now % 86400);

    if (ev.isVarying)
    {
        for (int t : ev.varyingTimes)
        {
            if (secondsOfDay < t)
                return t - secondsOfDay;   //. hasn't started yet
            if (secondsOfDay < t + ev.duration)
                return 0;                  //. active right now
            //_ else: already passed, check next index
        }
        //_ All times passed today, wrap to first one tomorrow.
        return 86400 - secondsOfDay + ev.varyingTimes[0];
    }

    //_ Periodic: phase = how far into the current cycle we are
    int phase = (((int)(now % ev.period) - ev.offset) % ev.period + ev.period) % ev.period;
    return ev.period - phase;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetSecondsUntilEventEnd   (group: GetSecondsUntilEventStart, IsEventActive)
//--------------------------------------------------------------------------------
// Returns how many seconds until the current active window closes. Only
// meaningful when IsEventActive() is true.
//--------------------------------------------------------------------------------
int GetSecondsUntilEventEnd(const WorldEvent& ev, time_t now)
{
    int secondsOfDay = (int)(now % 86400);

    if (ev.isVarying)
    {
        for (int t : ev.varyingTimes)
            if (secondsOfDay >= t && secondsOfDay < t + ev.duration)
                return t + ev.duration - secondsOfDay;
        return 0;
    }

    //_ Periodic: duration minus phase-into-cycle is the time left.
    int phase = (((int)(now % ev.period) - ev.offset) % ev.period + ev.period) % ev.period;
    return ev.duration - phase;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsEventActive   (group: GetSecondsUntilEventStart, GetSecondsUntilEventEnd)
//--------------------------------------------------------------------------------
// Returns true if the event is currently running.
//
// For varying events, checks directly whether now falls inside any active window.
// For periodic events, checks whether we are within the duration window at the
// start of the current cycle.
//--------------------------------------------------------------------------------
bool IsEventActive(const WorldEvent& ev, time_t now)
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

    int secs = GetSecondsUntilEventStart(ev, now);
    if (secs < 0) return false;
    return secs > (ev.period - ev.duration);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetZoomPercent
//--------------------------------------------------------------------------------
// Returns how "zoomed in" the full-screen map currently is, as 0-100. Mumble's
// Compass.Scale (continent units/pixel) gets SMALLER as you zoom in, but has no
// fixed documented min/max - it varies with screen resolution/UI scaling and, to
// a lesser extent, the current map. Instead of hardcoding a guessed range, this
// tracks the smallest/largest Compass.Scale values actually observed and
// interpolates within that: the scale self-calibrates the first time the user
// zooms fully in and out, and (backed by
// BasicEventZoomScaleMinObserved/MaxObserved in settings_table.h, not a local
// static) stays calibrated across restarts.
//--------------------------------------------------------------------------------
static float GetZoomPercent(float scale)
{
    if (scale <= 0.0f) return 0.0f;

    if (BasicEventZoomScaleMinObserved < 0.0f || scale < BasicEventZoomScaleMinObserved)
        BasicEventZoomScaleMinObserved = scale;
    if (BasicEventZoomScaleMaxObserved < 0.0f || scale > BasicEventZoomScaleMaxObserved)
        BasicEventZoomScaleMaxObserved = scale;

    float range = BasicEventZoomScaleMaxObserved - BasicEventZoomScaleMinObserved;
    if (range < 0.0001f) return 0.0f; //. no zoom variation observed yet

    //_ scale is inverse to zoom: smallest scale == 100% zoomed in.
    float pct = (BasicEventZoomScaleMaxObserved - scale) / range * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetEventZoomSizeMultiplier
//--------------------------------------------------------------------------------
// Markers stay at 1.0x from 0% zoom up to BasicEventZoomStartPct, then grow
// linearly to BasicEventZoomMaxMultiplier at 100% zoom. Declared in maprender.h
// (not static) so cyclicrender.cpp can reuse the exact same curve for cyclic
// group rings instead of duplicating this logic.
//--------------------------------------------------------------------------------
float GetEventZoomSizeMultiplier()
{
    float zoomPct = GetZoomPercent(MumbleLink->Context.Compass.Scale);

    if (!BasicEventZoomScalingEnabled) return 1.0f;

    float startPct = BasicEventZoomStartPct;
    if (startPct >= 100.0f) return 1.0f; //. growth window degenerate; never grow
    if (zoomPct <= startPct) return 1.0f;

    float t = (zoomPct - startPct) / (100.0f - startPct); //. 0..1 across growth window
    if (t > 1.0f) t = 1.0f;

    return 1.0f + t * (BasicEventZoomMaxMultiplier - 1.0f);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetContinentScale
//--------------------------------------------------------------------------------
// Continent units per screen pixel (Compass.Scale adjusted for display
// scaling), shared by ContinentToScreen, ScreenToContinent, and the
// live-event radius ring below.
//--------------------------------------------------------------------------------
static float GetContinentScale()
{
    float scale = MumbleLink->Context.Compass.Scale / NexusLink->Scaling;
    return scale < 0.0001f ? 1.0f : scale; //. guard against divide-by-zero on init
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContinentToScreen   (pairs with: ScreenToContinent)
//--------------------------------------------------------------------------------
// Maps a GW2 continent coordinate (cx, cy) to a screen pixel position.
//
// The Mumble compass gives us everything we need:
//   Compass.Center - the continent coordinate at the CENTER of the map view
//   Compass.Scale  - continent units per pixel (decreases as you zoom in)
//
// The center of the map on screen is the center of the window.
//--------------------------------------------------------------------------------
ImVec2 ContinentToScreen(float cx, float cy)
{
    const auto& compass = MumbleLink->Context.Compass;

    //_ Screen position Compass.Center maps to - the window center here.
    float screenCX = NexusLink->Width  * 0.5f;
    float screenCY = NexusLink->Height * 0.5f;

    float scale = GetContinentScale();

    return {
        screenCX + (cx - compass.Center.X) / scale,
        screenCY + (cy - compass.Center.Y) / scale
    };
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScreenToContinent   (pairs with: ContinentToScreen)
//--------------------------------------------------------------------------------
// Exact inverse of ContinentToScreen, used while drag-editing a marker's
// position: each frame the dragged marker's new screen position (mouse pos + the
// original click offset, see EditTarget handling below) needs to be converted
// back to a continent coordinate to store in continentX/Y.
//--------------------------------------------------------------------------------
ImVec2 ScreenToContinent(ImVec2 screenPos)
{
    const auto& compass = MumbleLink->Context.Compass;

    float screenCX = NexusLink->Width  * 0.5f;
    float screenCY = NexusLink->Height * 0.5f;

    float scale = GetContinentScale();

    return {
        compass.Center.X + (screenPos.x - screenCX) * scale,
        compass.Center.Y + (screenPos.y - screenCY) * scale
    };
}

//_ Shared edit-mode state - see the comment on EditModeState in maprender.h.
EditModeState g_EditMode;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearEditMode
//--------------------------------------------------------------------------------
void ClearEditMode()
{
    g_EditMode = EditModeState{};
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLiveEventRing
//--------------------------------------------------------------------------------
// Draws the bundled live-ring texture (resources/textures/live_ring.png) as a
// square quad of half-extent `radiusPx` centered on `pos`, rotated by
// `angleDeg` about that center. Every g_LiveEvents entry uses the same
// texture, so the rotation is what keeps a map full of them from looking
// like stamped copies of one image.
//--------------------------------------------------------------------------------
static void DrawLiveEventRing(ImDrawList* dl, ImTextureID tex, ImVec2 pos,
    float radiusPx, float angleDeg, ImU32 col)
{
    float rad = angleDeg * ((float)M_PI / 180.0f);
    float s   = sinf(rad);
    float c   = cosf(rad);

    //_ Unrotated corners relative to `pos`, rotated in place, then re-centered.
    ImVec2 corners[4] = {
        { -radiusPx, -radiusPx }, { radiusPx, -radiusPx },
        {  radiusPx,  radiusPx }, { -radiusPx, radiusPx },
    };
    for (ImVec2& corner : corners)
        corner = { pos.x + corner.x * c - corner.y * s,
                   pos.y + corner.x * s + corner.y * c };

    dl->AddImageQuad(tex, corners[0], corners[1], corners[2], corners[3],
        ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderMapEvents
//--------------------------------------------------------------------------------
// Draws a dot/icon for each g_Events entry, then (if ShowLiveEventMapDots) the
// live-ring texture per g_LiveEvents entry, sized to its radius (meters, same
// scale as continentX/Y - see GetContinentScale) and spun by DrawLiveEventRing.
// No hover, tooltip, drag-to-reposition, or per-map scoping for those.
// Basic Event size follows GetEventZoomSizeMultiplier.
//
// The overlay window keeps NoMouseInputs so it never blocks map-dragging;
// the armed marker's drag capture (EditModeState) is handled per-marker by
// DrawDragAnchor's anchor window, staying armed until "Drag"/"Stop" disarms it.
//--------------------------------------------------------------------------------
void RenderMapEvents()
{
    //_ Background draws before all ImGui content, including tooltips.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
 
    constexpr float RING_THICK = 1.5f;
    constexpr ImU32 COL_RING   = IM_COL32(255, 255, 255, 220); //. white ring
 
    time_t now = time(nullptr);
 
    //_ Same for every event this frame, computed once.
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
 
    for (int i = 0; i < (int)g_Events.size(); i++)
    {
        WorldEvent& ev = g_Events[i];
        bool isBeingEdited = (g_EditMode.target == EditTarget::BasicEvent && g_EditMode.index == i);
 
        ImVec2 pos = ContinentToScreen(ev.continentX, ev.continentY);
 
        //_ Off-screen culling, skipped while dragging - a fast drag can exceed this margin.
        if (!isBeingEdited)
        {
            if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
            if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;
        }
 
        //_ Map-only hide, exempted while dragging; doesn't touch the Subscriptions bar.
        if (!isBeingEdited && !ev.shown)
            continue;
 
        bool active = IsEventActive(ev, now);
        int  secs   = GetSecondsUntilEventStart(ev, now);
 
        //_ Upcoming events show only within the window; secs<0 and the edited marker exempt.
        if (!isBeingEdited && BasicEventTimeFilterEnabled && !active && secs >= 0 &&
            secs > BasicEventTimeFilterMinutes * 60)
            continue;
 
        //_ Plain user RGBA floats (settings_table.h); alpha comes from the swatch.
        ImU32 colFill = active                    ? ColorU32(BasicEventColorActive)
                      : (secs >= 0 && secs < 900) ? ColorU32(BasicEventColorSoon)
                      :                              ColorU32(BasicEventColorWaiting);
 
        Texture_t* icon = ev.iconTexture.empty() ? nullptr : GetOrRequestEventIcon(ev.iconTexture);
 
        //_ Tracks the drawn size (icon vs dot differ) so hover matches what's on screen.
        float hoverHalfExtent;
 
        if (icon && icon->Resource)
        {
            float halfW = BasicEventIconSize * zoomMult;
            float halfH = halfW * ((float)icon->Height / (float)icon->Width);
            dl->AddImage((ImTextureID)icon->Resource,
                ImVec2(pos.x - halfW, pos.y - halfH),
                ImVec2(pos.x + halfW, pos.y + halfH),
                ImVec2(0, 0), ImVec2(1, 1), colFill);
            //_ Width is usually icon art's dominant dimension; close enough for hover.
            hoverHalfExtent = halfW;
        }
        else
        {
            float radius = BasicEventDotRadius * zoomMult;
            dl->AddCircleFilled(pos, radius, colFill);
            dl->AddCircle(pos, radius, COL_RING, 0, RING_THICK);
            hoverHalfExtent = radius;
        }
 
        //_ Pulsing ring on the armed marker; shared with RenderCyclicGroups (map_shared.h).
        if (isBeingEdited)
            DrawEditPulseRing(dl, pos, hoverHalfExtent);
 
        bool hovered = ImGui::IsMouseHoveringRect(
            {pos.x - hoverHalfExtent, pos.y - hoverHalfExtent},
            {pos.x + hoverHalfExtent, pos.y + hoverHalfExtent});
 
        //_ Drag-capture architecture: see DrawDragAnchor (map_shared.h) and header above.
        if (isBeingEdited)
            DrawDragAnchor("##we_drag_anchor", i, pos, hoverHalfExtent,
                &ev.continentX, &ev.continentY);
 
        //_ Suppressed while dragging - the cursor sits on top of the marker.
        if (hovered && !isBeingEdited)
        {
            int secs = GetSecondsUntilEventStart(ev, now);
            if (secs < 0) continue; //. no timer data yet
 
            ImVec2 mouse = ImGui::GetMousePos();
            ImGui::SetNextWindowPos(
                {mouse.x - 1.0f, mouse.y - 20.0f},
                ImGuiCond_Always, {0.0f, 1.0f});
            ImGui::BeginTooltip();
            if (active)
            {
                int secsUntilEnd = GetSecondsUntilEventEnd(ev, now);
                ImGui::Text("%s — Active (ends in %s)",
                    ev.name.c_str(), FormatMinSec(secsUntilEnd).c_str());
            }
            else
            {
                ImGui::Text("%s — in %s",
                    ev.name.c_str(), FormatCountdown(secs).c_str());
            }
            ImGui::EndTooltip();
        }
    }

    if (ShowLiveEventMapDots)
    {
        float      scale   = GetContinentScale();
        Texture_t* ringTex = GetOrRequestEventIcon("live_ring.png");

        for (const LiveEvent& ev : g_LiveEvents)
        {
            ImVec2 pos      = ContinentToScreen(ev.continentX, ev.continentY);
            float  radiusPx = ev.radius / scale;

            if (pos.x + radiusPx < -100 || pos.x - radiusPx > NexusLink->Width  + 100) continue;
            if (pos.y + radiusPx < -100 || pos.y - radiusPx > NexusLink->Height + 100) continue;

            if (ringTex && ringTex->Resource)
            {
                //_ Per-event, stable, and free - no per-event rotation field needed.
                float angleDeg = fmodf(ev.continentX, 360.0f);
                DrawLiveEventRing(dl, (ImTextureID)ringTex->Resource, pos,
                    radiusPx, angleDeg, COL_RING);
            }
            else
            {
                dl->AddCircle(pos, radiusPx, COL_RING, 0, RING_THICK); //. texture still loading
            }
        }
    }

    ImGui::End();
}