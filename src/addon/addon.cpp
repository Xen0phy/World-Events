//################################################################################
// addon.cpp
//--------------------------------------------------------------------------------
// Nexus addon entry point: AddonLoad/AddonUnload (registered via
// GetAddonDef below) and AddonRender, the per-frame render callback that
// drives everything else (subscriptions views, map overlays).
//
// See SaveAllData below for the on-disk save-ordering rule shared by
// AddonLoad and AddonUnload.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "background_threads.h"
#include "cyclicrender.h"
#include "events.h"
#include "events_categories.h"
#include "events_storage.h"
#include "events_tracking.h"
#include "gw2_api.h"
#include "imgui.h"
#include "maprender.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_ui.h"
#include "version.h"

#include <filesystem>
#include <system_error>

AddonAPI_t*      APIDefs    = nullptr;
Mumble::Data*    MumbleLink = nullptr;
NexusLinkData_t* NexusLink  = nullptr;

std::string g_AddonDir;

float g_AvgRenderTimeMs        = 0.0f;
float g_AvgOptionsRenderTimeMs = 0.0f;

float g_AvgSubsBarDataMs      = 0.0f, g_AvgSubsBarDrawMs      = 0.0f;
float g_AvgSubsWindowDataMs   = 0.0f, g_AvgSubsWindowDrawMs   = 0.0f;
float g_AvgSubsNotifyDataMs   = 0.0f, g_AvgSubsNotifyDrawMs   = 0.0f;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveAllData
//--------------------------------------------------------------------------------
// Writes every on-disk JSON file this addon owns, in the one order that's
// actually safe: events first, then categories/subscriptions/tracking,
// since those three reference g_Events/g_CyclicGroups by name (see
// events_categories.h, subscriptions.h, events_tracking.h) and need
// events.json's own keys to already reflect the final merged state.
//
// Called from both AddonLoad (to persist merged defaults+disk state on
// first write) and AddonUnload, kept as one function so this ordering
// rule can't drift out of sync between the two callers. settings.ini is
// deliberately NOT included here - SaveSettings has no such ordering
// dependency and is only ever called from AddonUnload.
//--------------------------------------------------------------------------------
static void SaveAllData(const std::string& addonDir)
{
    SaveEventsData(addonDir);
    SaveCategoriesData(addonDir);     //. after events (see events_categories.h)
    SaveSubscriptionsData(addonDir);  //. after events (see subscriptions.h)
    SaveDailyTrackingData(addonDir);  //. after events (see events_tracking.h)
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetAllDataToDefaults
//--------------------------------------------------------------------------------
// Deletes events.json, then rebuilds g_Events/g_CyclicGroups from the
// compiled-in snapshot (ResetEventsToDefaults - a plain reload can't do
// this, since it would merge from the current, already-edited globals),
// reloads categories with the file gone (resolves to compiled-in
// defaults, same as AddonLoad's first-ever run), and explicitly clears
// subscriptions/done-today markers (see the note below on why reloading
// those two instead wouldn't actually clear them). SaveAllData then
// writes all of that back out as a fresh file.
//--------------------------------------------------------------------------------
bool ResetAllDataToDefaults()
{
    std::error_code ec;
    std::filesystem::remove(g_AddonDir + "\\events.json", ec);

    ResetEventsToDefaults();
    LoadCategoriesData(g_AddonDir); //. no file - resolves to compiled-in defaults

    //_ LoadSubscriptionsData/LoadDailyTrackingData both treat "no file" as
    // "nothing to load, leave memory as-is" (see their own comments) -
    // exactly wrong here, since memory still holds whatever was
    // subscribed/marked-done before the reset. Clear explicitly instead.
    ClearAllSubscriptions();
    ClearAllDoneMarkers();

    SaveAllData(g_AddonDir);
    return !ec;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonLoad
//--------------------------------------------------------------------------------
// Nexus calls this once, synchronously, before the addon does anything
// else. Order matters below: ImGui context first (nothing can render
// without it), then the four Load*Data calls (see each callee for the
// merge-with-compiled-defaults story), then SaveAllData to persist that
// merged state, then the render callbacks are registered last so nothing
// can render before setup has actually finished.
//--------------------------------------------------------------------------------
void AddonLoad(AddonAPI_t* aAPI)
{
    APIDefs = aAPI;

    //_ Share Nexus's ImGui context/allocators - without this the addon
    // has a separate context and nothing renders.
    ImGui::SetCurrentContext((ImGuiContext*)aAPI->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void*(*)(size_t, void*))aAPI->ImguiMalloc,
        (void (*)(void*, void*))aAPI->ImguiFree
    );

    MumbleLink = (Mumble::Data*)    APIDefs->DataLink_Get(DL_MUMBLE_LINK);
    NexusLink  = (NexusLinkData_t*) APIDefs->DataLink_Get(DL_NEXUS_LINK);

    g_AddonDir = APIDefs->Paths_GetAddonDirectory("WorldEvents");
    LoadSettings(g_AddonDir); //. missing file - keeps compiled defaults

    //_ g_Events/g_CyclicGroups already hold compiled-in defaults
    // (static-init time); this merges in whatever's saved on disk by
    // name, then SaveAllData below writes the merged result back out.
    LoadEventsData(g_AddonDir);

    //_ Compiled-in category defaults merged with events.json's own keys
    // by name (see events_categories.cpp); reads data_version itself, so
    // order vs LoadEventsData doesn't matter - only the SAVE order does.
    LoadCategoriesData(g_AddonDir);

    //_ No compiled-in defaults to merge (see subscriptions.h); read-order
    // doesn't matter here, only the save order below does.
    LoadSubscriptionsData(g_AddonDir);

    //_ Same story as subscriptions above; self-discards stale data from
    // a prior UTC day, so no rollover check is needed here.
    LoadDailyTrackingData(g_AddonDir);

    SaveAllData(g_AddonDir); //. see SaveAllData above

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Loaded.");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonUnload
//--------------------------------------------------------------------------------
// Deregisters render callbacks first so no new frame can start using
// data this is about to tear down, then waits (briefly, bounded) for any
// in-flight background thread before this DLL is unloaded out from under
// it, then persists everything and frees heap memory now, while the CRT
// is still intact.
//--------------------------------------------------------------------------------
void AddonUnload()
{
    //_ Stops any in-flight render calls before teardown starts.
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);

    //_ Bounded wait so a still-running background thread doesn't resume
    // in unloaded memory after this DLL is freed. 2s is generous
    // headroom (see gw2_api.cpp's shutdown hook), not a timeout budget.
    WaitForBackgroundThreads(2000);

    SaveSettings(g_AddonDir);
    SaveAllData(g_AddonDir);

    //_ Force heap frees now while the CRT is still intact, rather than
    // leaving it to the static destructor at DLL unload.
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonRender
//--------------------------------------------------------------------------------
// Per-frame render callback (RT_Render). Subscriptions views render
// whenever gameplay is active; map-only overlays (world markers, cyclic
// rings) additionally require the full-screen map to be open - see the
// early-outs below for exactly where that split happens.
//--------------------------------------------------------------------------------
void AddonRender()
{
    //_ No-op unless ShowDebug (see ScopedRenderTimer in addon.h); wraps
    // this whole function, including every early-return below.
    RenderTimer renderTimer;

    //_ Unlike the map overlays below, this watchlist is a normal ImGui
    // window - useful whether or not the full map is open, so it renders
    // unconditionally on IsGameplay; only the overlays need IsMapOpen too.
    if (MumbleLink && NexusLink && NexusLink->IsGameplay)
    {
        //_ Per-view kill-switches so at least one can stay on; also skips
        // PollGw2Api itself once none of the three would consume its
        // data anyway - no point polling for nothing to show.
        bool isCompetitive = MumbleLink->Context.IsCompetitive;
        bool allDisabled = DisableWindowWhenCompetitive && DisableBarWhenCompetitive && DisableNotifyWhenCompetitive;

        if (!(isCompetitive && allDisabled))
        {
            //_ Cheap no-op most frames (internal rate limiting) - called here
            // alongside the two views that actually consume its data.
            PollGw2Api();

            if (!(isCompetitive && DisableWindowWhenCompetitive)) RenderSubscriptionsWindow();
            if (!(isCompetitive && DisableBarWhenCompetitive))    RenderSubscriptionsBar();
            if (!(isCompetitive && DisableNotifyWhenCompetitive)) RenderSubscriptionsNotifications();
        }
    }

    if (!MumbleLink || !NexusLink)          return;
    if (!NexusLink->IsGameplay)             return;

    //_ Edit mode (see maprender.h) must not stay armed once the map
    // closes, or reopening it later could resume dragging with no visual
    // cue. Needs the FALLING edge specifically, not just IsMapOpen == false.
    static bool wasMapOpen = false;
    bool isMapOpen = MumbleLink->Context.IsMapOpen;
    if (wasMapOpen && !isMapOpen)
        ClearEditMode();
    wasMapOpen = isMapOpen;

    if (!isMapOpen) return;

    //_ PvP/WvW maps never have Basic/Cyclic events on them - unconditional,
    // not tied to any setting (unlike the subscriptions views above).
    if (MumbleLink->Context.IsCompetitive) return;

    RenderMapEvents();
    if (ShowCyclicOverlay)
        RenderCyclicGroups();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetAddonDef
//--------------------------------------------------------------------------------
// Required Nexus export; returns the static AddonDefinition_t Nexus reads
// once at load to get metadata and the Load/Unload function pointers.
//--------------------------------------------------------------------------------
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonDefinition_t def;
    def.Signature   = 0x57455645; //. "WEVE"
    def.APIVersion  = NEXUS_API_VERSION;
    def.Name        = "World Events";
    def.Version     = {Maj, Min, Bld, Rev};
    def.Author      = "Xenophy.2716";
    def.Description = "An event timer that's not just a plain stack of bars. Shows meta-events on the world map.";
    def.Load        = AddonLoad;
    def.Unload      = AddonUnload;
    def.Flags       = AF_None;
    def.Provider    = UP_GitHub;
    def.UpdateLink  = "https://github.com/Xen0phy/World-Events";
    return &def;
}