// addon.cpp
// Nexus addon entry point: AddonLoad/AddonUnload (registered via
// GetAddonDef below) and AddonRender, the per-frame render callback that
// drives everything else (subscriptions views, map overlays).
//
// See SaveAllData below for the on-disk save-ordering rule shared by
// AddonLoad and AddonUnload.

#include "addon.h"
#include "events.h"
#include "maprender.h"
#include "cyclicrender.h"
#include "settings.h"
#include "background_threads.h"
#include "events_storage.h"
#include "events_categories.h"
#include "subscriptions.h"
#include "events_tracking.h"
#include "gw2_api.h"
#include "imgui.h"
#include "version.h"

AddonAPI_t*      APIDefs    = nullptr;
Mumble::Data*    MumbleLink = nullptr;
NexusLinkData_t* NexusLink  = nullptr;

std::string g_AddonDir;

float g_AvgRenderTimeMs        = 0.0f;
float g_AvgOptionsRenderTimeMs = 0.0f;

float g_AvgSubsBarDataMs      = 0.0f, g_AvgSubsBarDrawMs      = 0.0f;
float g_AvgSubsWindowDataMs   = 0.0f, g_AvgSubsWindowDrawMs   = 0.0f;
float g_AvgSubsNotifyDataMs   = 0.0f, g_AvgSubsNotifyDrawMs   = 0.0f;

// RenderTimer / OptionsRenderTimer (debug only — see ShowDebug in addon.h)
// wrap AddonRender's and AddonOptions's entire bodies respectively,
// including AddonRender's several early-return paths (not-in-gameplay /
// map-closed early-outs below) — a destructor-based timer catches every
// one of those on its own, rather than needing a matching "stop the
// clock" line duplicated at each return statement. See ScopedRenderTimer
// in addon.h for the shared implementation and why the two don't mix.

// ---------------------------------------------------------------------------
// SaveAllData
// ---------------------------------------------------------------------------
// Writes every on-disk JSON/ini-adjacent data file this addon owns, in the
// one order that's actually safe: events first, then categories/
// subscriptions/tracking. Those three all reference g_Events/g_CyclicGroups
// by name (see events_categories.h, subscriptions.h, events_tracking.h), so
// events.json's own "events"/"cyclicGroups" keys have to already reflect
// the final, merged in-memory state before any of the others are written —
// otherwise a name lookup on the NEXT load could fail to resolve against
// whatever's sitting in the file right now.
//
// Called from both AddonLoad (to persist the merged defaults+disk state on
// first write) and AddonUnload. Kept as a single function rather than left
// as a repeated four-line block at each call site specifically so this
// ordering rule has exactly one place to read, and can't drift out of sync
// between the two callers if a line ever gets reordered at only one of
// them. settings.ini is deliberately NOT included here — SaveSettings is
// unrelated to this ordering (no cross-referencing by name) and is only
// ever called from AddonUnload, not AddonLoad.
// ---------------------------------------------------------------------------
static void SaveAllData(const std::string& addonDir)
{
    SaveEventsData(addonDir);
    SaveCategoriesData(addonDir);     // must run AFTER SaveEventsData — see events_categories.h
    SaveSubscriptionsData(addonDir);  // must run AFTER SaveEventsData — see subscriptions.h
    SaveDailyTrackingData(addonDir);  // must run AFTER SaveEventsData — see events_tracking.h
}

void AddonLoad(AddonAPI_t* aAPI)
{
    APIDefs = aAPI;

    // Share the ImGui context and allocators Nexus is already using.
    // Without this the addon has a separate context and nothing renders.
    ImGui::SetCurrentContext((ImGuiContext*)aAPI->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void*(*)(size_t, void*))aAPI->ImguiMalloc,
        (void (*)(void*, void*))aAPI->ImguiFree
    );

    // Grab the shared data pointers
    MumbleLink = (Mumble::Data*)    APIDefs->DataLink_Get(DL_MUMBLE_LINK);
    NexusLink  = (NexusLinkData_t*) APIDefs->DataLink_Get(DL_NEXUS_LINK);

    g_AddonDir = APIDefs->Paths_GetAddonDirectory("WorldEvents");
    LoadSettings(g_AddonDir); // missing file -> globals keep settings_table.h defaults

    // g_Events / g_CyclicGroups are already populated with the compiled-in
    // defaults at this point (events_basic.cpp / events_cyclic.cpp run at static-init
    // time, before AddonLoad). LoadEventsData merges those defaults with
    // whatever's saved on disk (by name — see events_storage.cpp) and
    // replaces g_Events / g_CyclicGroups with the merged result. Missing
    // file -> defaults are left untouched, and the SaveEventsData call
    // below then writes them out so the file exists from this run on.
    LoadEventsData(g_AddonDir);

    // g_DefaultBasicCategories / g_DefaultCyclicCategories are the
    // compiled-in category defaults (events_basic.cpp / events_cyclic.cpp),
    // merged with events.json's "basicCategories"/"cyclicCategories" by
    // name — see events_categories.cpp. LoadCategoriesData reads the on-disk
    // "data_version" itself (same key LoadEventsData reads), so it doesn't
    // depend on LoadEventsData having run first; order relative to
    // LoadEventsData still doesn't matter here — only the SAVE order below
    // does, see events_categories.h.
    LoadCategoriesData(g_AddonDir);

    // Same file, same "no compiled-in defaults to merge" story as
    // categories — see subscriptions.h. Order relative to the other
    // Load*Data calls doesn't matter (it only reads its own two keys),
    // only the SAVE order below does.
    LoadSubscriptionsData(g_AddonDir);

    // Same file/order story as subscriptions above — see events_tracking.h.
    // Reads its own stored UTC day and self-discards if it's from a prior
    // day, so no explicit rollover check is needed here.
    LoadDailyTrackingData(g_AddonDir);

    SaveAllData(g_AddonDir); // writes back merged defaults+disk state — see SaveAllData above

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Loaded.");
}

void AddonUnload()
{
    // Deregister first — stop any in-flight render calls
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);

    // Both PollGw2Api (gw2_api.cpp) and PasteToChat (subscriptions.cpp)
    // fire off detached background threads. Nothing above stops one that's
    // already running, and this DLL is typically FreeLibrary()'d shortly
    // after this function returns — if such a thread is still executing
    // when that happens, it resumes running code that no longer exists in
    // memory. GUI_Deregister above means no *new* one can start from here
    // on, so this blocks (briefly, and only if one happens to be mid-
    // flight right now) until every one already running has actually
    // finished, or bails out past a short bound rather than hanging the
    // whole unload.
    //
    // 2s, not the ~30s a stuck WinHTTP call could otherwise take: gw2_api's
    // registered shutdown hook force-closes any in-flight HTTP request the
    // instant this is called (see background_threads.h / gw2_api.cpp), so
    // that thread should reach its next checkpoint and exit almost
    // immediately rather than this wait actually needing to cover a full
    // network timeout. 2s is just headroom for that to happen (plus
    // PasteToChat's short, uncancellable keystroke-delay sleeps) — not a
    // budget for a hung request. Kept short on purpose: if the host ever
    // calls AddonUnload as part of closing the whole game, a long wait here
    // would stall that exit, and if the process gets killed outright
    // (Task Manager, or the OS tearing it down after the window closes)
    // every thread is torn down together regardless of this wait, so
    // there's nothing to gain by making it longer "just in case" — it only
    // matters for the "just this DLL gets unloaded, game keeps running"
    // case, which is the fast/common one.
    WaitForBackgroundThreads(2000);

    SaveSettings(g_AddonDir);
    SaveAllData(g_AddonDir);

    // Force heap frees now while the CRT is still intact,
    // rather than leaving it to the static destructor at DLL unload.
    g_Events.clear();
    g_Events.shrink_to_fit();

    g_CyclicGroups.clear();
    g_CyclicGroups.shrink_to_fit();

    g_BasicCategories.clear();
    g_BasicCategories.shrink_to_fit();
    g_CyclicCategories.clear();
    g_CyclicCategories.shrink_to_fit();

    g_SubscribedBasicEvents.clear();
    g_SubscribedBasicEvents.shrink_to_fit();
    g_SubscribedCyclicSlots.clear();
    g_SubscribedCyclicSlots.shrink_to_fit();

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Unloaded.");
}

void AddonRender()
{
    RenderTimer renderTimer; // no-op unless ShowDebug is true — see its own comment above

    // The subscriptions watchlist is a normal ImGui window (not a
    // background-drawlist overlay onto the game world like the map
    // markers/rings below), so it's useful — and expected to keep working
    // — whether or not the full-screen map is currently open. It's drawn
    // unconditionally on IsGameplay alone; the early-out above it only
    // gates the two map-only overlays.
    if (MumbleLink && NexusLink && NexusLink->IsGameplay)
    {
        // Cheap no-op most frames (see PollGw2Api's internal rate
        // limiting) — called here, alongside the two views that actually
        // consume its data, rather than unconditionally every frame
        // regardless of gameplay state.
        PollGw2Api();

        RenderSubscriptionsWindow();
        RenderSubscriptionsBar();
        RenderSubscriptionsNotifications();
    }

    if (!MumbleLink || !NexusLink)          return;
    if (!NexusLink->IsGameplay)             return;

    // Edit mode (drag-to-reposition, armed via the "Drag" button next to
    // an event/group's Location field — see maprender.h) must never stay
    // silently armed once the map is closed — otherwise reopening the map
    // later could immediately start dragging whatever was being edited
    // last time, with no visual cue why. Tracked via a simple last-frame
    // flag rather than relying on IsMapOpen alone, since we need the
    // FALLING edge (open -> closed) specifically, not just "currently
    // closed".
    static bool wasMapOpen = false;
    bool isMapOpen = MumbleLink->Context.IsMapOpen;
    if (wasMapOpen && !isMapOpen)
        ClearEditMode();
    wasMapOpen = isMapOpen;

    if (!isMapOpen) return;

    RenderMapEvents();
    if (ShowCyclicOverlay)
        RenderCyclicGroups();
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonDefinition_t def;
    def.Signature   = 0x57455645; // "WEVE", fits uint32_t — change before release
    def.APIVersion  = NEXUS_API_VERSION;
    def.Name        = "World Events";
    def.Version     = {Maj, Min, Bld, Rev};
    def.Author      = "xenophy";
    def.Description = "Meta event timers on the world map";
    def.Load        = AddonLoad;
    def.Unload      = AddonUnload;
    def.Flags       = AF_None;
    def.Provider    = UP_None;
    def.UpdateLink  = nullptr;
    return &def;
}