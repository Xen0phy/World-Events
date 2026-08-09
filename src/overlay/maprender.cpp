//################################################################################
// maprender.cpp
//--------------------------------------------------------------------------------
// EventIconEntry/s_iconCache   per-filename cache of loaded icon textures
// GetOrRequestEventIcon         returns/kicks off the load for one icon
// GetSecondsUntilEventStart/GetSecondsUntilEventEnd/IsEventActive
//                               Basic Event schedule queries (see maprender.h)
// GetEventZoomSizeMultiplier   current zoom-based marker size multiplier
// ContinentToScreen/ScreenToContinent
//                               continent coordinate <-> screen pixel
// g_EditMode/ClearEditMode     shared drag-to-reposition state
// RenderMapEvents               draws all Basic Events onto the open world map
//--------------------------------------------------------------------------------
// Icon textures are optional, per-event, and either user-supplied or bundled
// default. Scans a "textures" folder under the addon's own directory, loads
// on demand via Nexus's async Textures_LoadFromFile (never
// Textures_GetOrCreateFromFile, which can hand back a Texture_t* whose
// ->Resource is still null while the file decodes in the background, with no
// signal for when it becomes ready), and caches the result by filename - an
// arbitrary number of DIFFERENT events can each reference a different icon
// filename, so this needs a cache keyed by filename, not a fixed slot.
//
// A small set of default icons ships compiled into the dll itself (see
// events_icons.h / s_defaultIcons below), loaded via Textures_LoadFromMemory
// instead of LoadFromFile so no files need to exist on disk for these to
// work out of the box. Disk is always checked FIRST for a given filename -
// see GetOrRequestEventIcon - so a user can still override/reskin a bundled
// icon by dropping a same-named file into their own textures/ folder.
//
// AUTHORING REQUIREMENT: because the icon is recolored at draw time via a
// multiplicative tint (ImDrawList::AddImage's `col` parameter multiplies
// every pixel's RGB/A by the tint color), the source image's RGB needs to
// already be a NEUTRAL GRAY (not necessarily pure white - any gray works,
// preserving relative shading/"shadows" within the icon) with the actual
// icon shape carried in the alpha channel. A full-color icon will tint
// unpredictably rather than cleanly recolor, since multiplying non-gray RGB
// by a tint shifts its hue instead of replacing it. This addon does NOT
// desaturate arbitrary images automatically - that would need a real image
// decoder (e.g. stb_image) to get at raw pixels before upload, which is
// more than this addon's scope calls for; the user is expected to prepare a
// gray/alpha icon themselves (e.g. desaturate + add a layer mask in any
// image editor) before dropping it in the textures folder.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "color_utils.h"
#include "events.h"
#include "events_icons.h"
#include "imgui.h"
#include "map_shared.h"
#include "maprender.h"
#include "settings.h"
#include "time_format.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

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

//_ Guards s_iconCache: Nexus's texture-load callback (OnEventIconReceived)
// runs on a background thread while GetOrRequestEventIcon reads/inserts
// from the render thread - unsynchronized, that's a real race.
static std::mutex s_iconCacheMutex;
static std::unordered_map<std::string, EventIconEntry> s_iconCache;

static std::vector<std::string> s_iconFilenames;
static bool s_iconFilenamesScanned = false;

//********************************************************************************
// DefaultIconEntry
//--------------------------------------------------------------------------------
// name    bundled filename, matched against disk/user references
// data    raw PNG bytes (see events_icons.h)
// size    byte length of `data`
//--------------------------------------------------------------------------------
// One entry per bundled icon (s_defaultIcons below). Only consulted when
// nothing matching `name` exists on disk, see GetOrRequestEventIcon
//--------------------------------------------------------------------------------
struct DefaultIconEntry
{
    const char*    name;
    const uint8_t* data;
    uint64_t       size;
};

static const DefaultIconEntry s_defaultIcons[] =
{
    { "BasicCross.png",  g_BasicIconData,       g_BasicIconData_size },
    { "Convergence.png", g_ConvergenceIconData, g_ConvergenceIconData_size },
    { "EventBoss.png",   g_EventBossIconData,   g_EventBossIconData_size },
    { "EventMap.png",    g_EventMapIconData,    g_EventMapIconData_size },
    { "Festival.png",    g_FestivalIconData,    g_FestivalIconData_size },
    { "WorldBoss.png",   g_WorldBossIconData,   g_WorldBossIconData_size },
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FindDefaultIcon
//--------------------------------------------------------------------------------
static const DefaultIconEntry* FindDefaultIcon(const std::string& filename)
{
    for (const auto& entry : s_defaultIcons)
        if (filename == entry.name)
            return &entry;
    return nullptr;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScanEventIconFiles   (pairs with: GetEventIconFilenames)
//--------------------------------------------------------------------------------
// Scan (or re-scan) "<addon dir>/textures" and rebuild s_iconFilenames.
// Call this to refresh after the user adds new files - there's no
// automatic filesystem-watching.
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

    for (const auto& defaultIcon : s_defaultIcons)
        if (std::find(s_iconFilenames.begin(), s_iconFilenames.end(), defaultIcon.name) == s_iconFilenames.end())
            s_iconFilenames.push_back(defaultIcon.name);

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
// Returns the loaded Texture_t* for this filename, or nullptr if it's not
// loaded yet (including: load not yet requested, in which case this also
// kicks off an async request).
//
// DISK FIRST: if a file by this name exists under <addonDir>/textures,
// it's loaded from there exactly as before, even if the same name also
// exists in s_defaultIcons - this is what lets a user override/reskin a
// bundled icon just by dropping a same-named file, no rebuild needed.
// Only when nothing matches on disk does this fall back to the bundled
// table via Textures_LoadFromMemory instead of Textures_LoadFromFile.
//--------------------------------------------------------------------------------
static Texture_t* GetOrRequestEventIcon(const std::string& filename)
{
    if (filename.empty()) return nullptr;

    //_ Only the cache lookup/insert itself needs to be under the lock -
    // the actual load dispatch below talks to Nexus and shouldn't happen
    // while holding it.
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

        //_ Identifier must be unique per filename so OnEventIconReceived
        // can route the callback back to the right cache entry
        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec))
        {
            APIDefs->Textures_LoadFromFile(filename.c_str(), fullPath.c_str(), OnEventIconReceived);
        }
        else if (const DefaultIconEntry* bundled = FindDefaultIcon(filename))
        {
            //_ aData is a non-const void* in the Nexus signature even
            // though this call only reads/decodes it
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
// Returns how many seconds until the current active window closes.
// Only meaningful when IsEventActive() is true.
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

    //_ Periodic: phase is how far into the cycle we are, duration - phase
    // is the time left.
    int phase = (((int)(now % ev.period) - ev.offset) % ev.period + ev.period) % ev.period;
    return ev.duration - phase;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsEventActive   (group: GetSecondsUntilEventStart, GetSecondsUntilEventEnd)
//--------------------------------------------------------------------------------
// Returns true if the event is currently running.
//
// For varying events, checks directly whether now falls inside any active
// window. For periodic events, checks whether we are within the duration
// window at the start of the current cycle.
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
// Returns how "zoomed in" the full-screen map currently is, as 0-100.
// Mumble's Compass.Scale (continent units/pixel) gets SMALLER as you zoom
// in, but has no fixed documented min/max - it varies with screen
// resolution/UI scaling and, to a lesser extent, the current map. Rather
// than hardcode a guessed range, we track the smallest/largest
// Compass.Scale values actually observed and interpolate within that: the
// scale self-calibrates the first time the user zooms fully in and out,
// and (backed by BasicEventZoomScaleMinObserved/MaxObserved in
// settings_table.h, not a local static) stays calibrated across restarts.
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
// linearly to BasicEventZoomMaxMultiplier at 100% zoom. Declared in
// maprender.h (not static) so cyclicrender.cpp can reuse the exact same
// curve for cyclic group rings instead of duplicating this logic.
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

    //_ Screen position that Compass.Center maps to.
    // For the full-screen map this is the window center.
    float screenCX = NexusLink->Width  * 0.5f;
    float screenCY = NexusLink->Height * 0.5f;

    float scale = compass.Scale / NexusLink->Scaling;
    if (scale < 0.0001f) scale = 1.0f; //. guard against divide-by-zero on init

    return {
        screenCX + (cx - compass.Center.X) / scale,
        screenCY + (cy - compass.Center.Y) / scale
    };
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScreenToContinent   (pairs with: ContinentToScreen)
//--------------------------------------------------------------------------------
// Exact inverse of ContinentToScreen, used while drag-editing a marker's
// position: each frame the dragged marker's new screen position (mouse pos
// + the original click offset, see EditTarget handling below) needs to be
// converted back to a continent coordinate to store in continentX/Y.
//--------------------------------------------------------------------------------
ImVec2 ScreenToContinent(ImVec2 screenPos)
{
    const auto& compass = MumbleLink->Context.Compass;

    float screenCX = NexusLink->Width  * 0.5f;
    float screenCY = NexusLink->Height * 0.5f;

    float scale = compass.Scale / NexusLink->Scaling;
    if (scale < 0.0001f) scale = 1.0f;

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
// RenderMapEvents
//--------------------------------------------------------------------------------
// Draws a dot/icon for each event in g_Events. Size is normally fixed in
// pixels regardless of map zoom (zoom is already baked into Compass.Scale,
// which we only use to position the center) - but if BasicEventZoomScaling
// is enabled, markers additionally grow as the map is zoomed in past
// BasicEventZoomStartPct, up to BasicEventZoomMaxMultiplier at 100% zoom.
// See GetZoomPercent()/GetEventZoomSizeMultiplier() above.
//
// The full-screen overlay window below always keeps NoMouseInputs, so it
// never blocks map-dragging. Drag capture for whichever one marker is
// armed (see EditModeState in maprender.h) is instead handled per-marker
// by DrawDragAnchor's small anchor window, recreated every frame at the
// marker's position - it stays armed across multiple press-drag-release
// cycles, until the panel's "Drag"/"Stop" button disarms it.
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
 
        //_ Off-screen culling, skipped while being dragged - a fast drag
        // can momentarily push the cursor outside this margin.
        if (!isBeingEdited)
        {
            if (pos.x < -100 || pos.x > NexusLink->Width  + 100) continue;
            if (pos.y < -100 || pos.y > NexusLink->Height + 100) continue;
        }
 
        //_ Map-only hide; doesn't touch the Subscriptions bar/window.
        // Exempted while being dragged.
        if (!isBeingEdited && !ev.shown)
            continue;
 
        bool active = IsEventActive(ev, now);
        int  secs   = GetSecondsUntilEventStart(ev, now);
 
        //_ Upcoming events show only within the configured window; secs<0
        // (no timer data) and the currently-edited marker are exempt.
        if (!isBeingEdited && BasicEventTimeFilterEnabled && !active && secs >= 0 &&
            secs > BasicEventTimeFilterMinutes * 60)
            continue;
 
        //_ Plain user RGBA floats (settings_table.h/color_utils.h); alpha
        // comes straight from the swatch, no separate hardcoded alpha.
        ImU32 colFill = active                    ? ColorU32(BasicEventColorActive)
                      : (secs >= 0 && secs < 900) ? ColorU32(BasicEventColorSoon)
                      :                              ColorU32(BasicEventColorWaiting);
 
        Texture_t* icon = ev.iconTexture.empty() ? nullptr : GetOrRequestEventIcon(ev.iconTexture);
 
        //_ Tracks whichever size is actually drawn (icon vs dot differ),
        // so the hover/tooltip rect matches what's on screen.
        float hoverHalfExtent;
 
        if (icon && icon->Resource)
        {
            float halfW = BasicEventIconSize * zoomMult;
            float halfH = halfW * ((float)icon->Height / (float)icon->Width);
            dl->AddImage((ImTextureID)icon->Resource,
                ImVec2(pos.x - halfW, pos.y - halfH),
                ImVec2(pos.x + halfW, pos.y + halfH),
                ImVec2(0, 0), ImVec2(1, 1), colFill);
            //_ width is usually the dominant dimension for icon art;
            // close enough for a hover box.
            hoverHalfExtent = halfW;
        }
        else
        {
            float radius = BasicEventDotRadius * zoomMult;
            dl->AddCircleFilled(pos, radius, colFill);
            dl->AddCircle(pos, radius, COL_RING, 0, RING_THICK);
            hoverHalfExtent = radius;
        }
 
        //_ Pulsing ring on the marker armed for drag-to-reposition.
        // Shared with RenderCyclicGroups - see overlay/map_shared.h.
        if (isBeingEdited)
            DrawEditPulseRing(dl, pos, hoverHalfExtent);
 
        bool hovered = ImGui::IsMouseHoveringRect(
            {pos.x - hoverHalfExtent, pos.y - hoverHalfExtent},
            {pos.x + hoverHalfExtent, pos.y + hoverHalfExtent});
 
        //_ See DrawDragAnchor's comment (map_shared.h) and the function
        // header above for the drag-capture architecture.
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

    ImGui::End();
}