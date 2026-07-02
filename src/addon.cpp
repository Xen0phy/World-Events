#include "addon.h"
#include "cyclic.h"
#include "events.h"
#include "maprender.h"
#include "cyclicrender.h"
#include "settings.h"
#include "events_storage.h"
#include "categories.h"
#include "subscriptions.h"
#include "subscriptions_window.h"
#include "subscriptions_bar.h"
#include "imgui.h"
#include "version.h"

AddonAPI_t*      APIDefs    = nullptr;
Mumble::Data*    MumbleLink = nullptr;
NexusLinkData_t* NexusLink  = nullptr;

std::string g_AddonDir;

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
    // defaults at this point (events.cpp / cyclic.cpp run at static-init
    // time, before AddonLoad). LoadEventsData merges those defaults with
    // whatever's saved on disk (by name — see events_storage.cpp) and
    // replaces g_Events / g_CyclicGroups with the merged result. Missing
    // file -> defaults are left untouched, and the SaveEventsData call
    // below then writes them out so the file exists from this run on.
    LoadEventsData(g_AddonDir);

    // Categories have no compiled-in defaults to merge against (they're
    // entirely user-created), so this just loads straight from the same
    // events.json file. Order relative to LoadEventsData doesn't matter
    // here — only the SAVE order below does, see categories.h.
    LoadCategoriesData(g_AddonDir);

    // Same file, same "no compiled-in defaults to merge" story as
    // categories — see subscriptions.h. Order relative to the other
    // Load*Data calls doesn't matter (it only reads its own two keys),
    // only the SAVE order below does.
    LoadSubscriptionsData(g_AddonDir);

    SaveEventsData(g_AddonDir);
    SaveCategoriesData(g_AddonDir);     // must run AFTER SaveEventsData — see categories.h
    SaveSubscriptionsData(g_AddonDir);  // must run AFTER SaveEventsData — see subscriptions.h

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Loaded.");
}

void AddonUnload()
{
    // Deregister first — stop any in-flight render calls
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);

    SaveSettings(g_AddonDir);
    SaveEventsData(g_AddonDir);
    SaveCategoriesData(g_AddonDir);     // must run AFTER SaveEventsData — see categories.h
    SaveSubscriptionsData(g_AddonDir);  // must run AFTER SaveEventsData — see subscriptions.h

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
    // The subscriptions watchlist is a normal ImGui window (not a
    // background-drawlist overlay onto the game world like the map
    // markers/rings below), so it's useful — and expected to keep working
    // — whether or not the full-screen map is currently open. It's drawn
    // unconditionally on IsGameplay alone; the early-out above it only
    // gates the two map-only overlays.
    if (MumbleLink && NexusLink && NexusLink->IsGameplay)
    {
        RenderSubscriptionsWindow();
        RenderSubscriptionsBar();
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
