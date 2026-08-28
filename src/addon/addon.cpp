//################################################################################
// addon.cpp   (see: addon.h)
//--------------------------------------------------------------------------------
// Nexus addon entry point: AddonLoad/AddonUnload (registered via GetAddonDef
// below) and AddonRender, the per-frame render callback that drives everything
// else (subscriptions views, map overlays).
//
// See SaveAllData below for the on-disk save-ordering rule shared by AddonLoad
// and AddonUnload.
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
#include "live_events_ui.h"
#include "maprender.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_edit_window.h"
#include "subscriptions_ui.h"
#include "version.h"
#include "ws_client.h"
#include "ws_debug_log.h"
#include "ws_debug_window.h"

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
// actually safe: events first, then categories/subscriptions/tracking, since
// those three reference g_Events/g_CyclicGroups by name (see events_categories.h,
// subscriptions.h, events_tracking.h) and need events.json's own keys to already
// reflect the final merged state.
//
// Called from both AddonLoad (to persist merged defaults+disk state on first
// write) and AddonUnload, kept as one function so this ordering rule can't drift
// out of sync between the two callers. settings.ini is deliberately NOT included
// here - SaveSettings has no such ordering dependency and is only ever called
// from AddonUnload.
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
// Deletes events.json, then rebuilds g_Events/g_CyclicGroups from the compiled-in
// snapshot (ResetEventsToDefaults - a plain reload can't do this, since it would
// merge from the current, already-edited globals), reloads categories with the
// file gone (resolves to compiled-in defaults, same as AddonLoad's first-ever
// run), and explicitly clears subscriptions/done-today markers (see the note
// below on why reloading those two instead wouldn't actually clear them).
// SaveAllData then writes all of that back out as a fresh file.
//--------------------------------------------------------------------------------
bool ResetAllDataToDefaults()
{
    std::error_code ec;
    std::filesystem::remove(g_AddonDir + "\\events.json", ec);

    ResetEventsToDefaults();
    LoadCategoriesData(g_AddonDir); //. no file - resolves to compiled-in defaults

    //_ LoadSubscriptionsData/LoadDailyTrackingData treat "no file" as "leave memory as-is" - wrong here; clear explicitly.
    ClearAllSubscriptions();
    ClearAllDoneMarkers();

    SaveAllData(g_AddonDir);
    return !ec;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonLoad
//--------------------------------------------------------------------------------
// Nexus calls this once, synchronously, before the addon does anything else.
// Order matters below: ImGui context first (nothing can render without it), then
// the four Load*Data calls (see each callee for the merge-with-compiled-defaults
// story), then SaveAllData to persist that merged state, then the render
// callbacks are registered last so nothing can render before setup has actually
// finished.
//--------------------------------------------------------------------------------
void AddonLoad(AddonAPI_t* aAPI)
{
    APIDefs = aAPI;

    //_ Share Nexus's ImGui context/allocators - without this the addon has a separate context and nothing renders.
    ImGui::SetCurrentContext((ImGuiContext*)aAPI->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void*(*)(size_t, void*))aAPI->ImguiMalloc,
        (void (*)(void*, void*))aAPI->ImguiFree
    );

    MumbleLink = (Mumble::Data*)    APIDefs->DataLink_Get(DL_MUMBLE_LINK);
    NexusLink  = (NexusLinkData_t*) APIDefs->DataLink_Get(DL_NEXUS_LINK);

    //_ Set before InitWsDebugLog/InitWsClient below so the very first connect attempt this session is captured, not just later ones.
    g_AddonDir = APIDefs->Paths_GetAddonDirectory("WorldEvents");

    //_ Opens ws_traffic.log and mirrors into Nexus's log; must precede InitWsClient so nothing it logs is dropped (see ws_debug_log.h).
    InitWsDebugLog(g_AddonDir);

    //_ Idle until the first UpdateShard call, wired in alongside the report button UI (see ws_client.h).
    InitWsClient();

    LoadSettings(g_AddonDir); //. missing file - keeps compiled defaults

    //_ g_Events/g_CyclicGroups already hold compiled-in defaults; this merges in disk state by name, saved back below.
    LoadEventsData(g_AddonDir);

    //_ Compiled-in category defaults merged with events.json by name; reads data_version, so load order doesn't matter.
    LoadCategoriesData(g_AddonDir);

    //_ No compiled-in defaults to merge (see subscriptions.h); read-order doesn't matter, only save order does.
    LoadSubscriptionsData(g_AddonDir);

    //_ Same story as subscriptions above; self-discards stale data from a prior UTC day, no rollover check needed.
    LoadDailyTrackingData(g_AddonDir);

    SaveAllData(g_AddonDir); //. see SaveAllData above

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    //_ Registered separately from AddonRender so it isn't gated by its IsGameplay/IsMapOpen early-outs; connects can happen at character select too.
    APIDefs->GUI_Register(RT_Render, RenderWsDebugWindow);

    //_ Grants Esc-to-close to the Edit Subscriptions window.
    APIDefs->GUI_RegisterCloseOnEscape(kEditSubscriptionsWindowTitle, &ShowEditSubscriptionsWindow);

    //_ Same, for the live-event recent-reports window (live_events_ui.h).
    APIDefs->GUI_RegisterCloseOnEscape(kLiveEventReportsWindowTitle, &ShowLiveEventReportsWindow);

    //_ Same, for the WS debug log window (ws_debug_window.h).
    APIDefs->GUI_RegisterCloseOnEscape(kWsDebugWindowTitle, &ShowWsDebugWindow);

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Loaded.");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonUnload
//--------------------------------------------------------------------------------
// Deregisters render callbacks first so no new frame can start using data this is
// about to tear down, then waits (briefly, bounded) for any in-flight background
// thread before this DLL is unloaded out from under it, then persists everything
// and frees heap memory now, while the CRT is still intact.
//--------------------------------------------------------------------------------
void AddonUnload()
{
    //_ Stops any in-flight render calls before teardown starts.
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(RenderWsDebugWindow);

    //_ Matches the GUI_RegisterCloseOnEscape calls in AddonLoad
    APIDefs->GUI_DeregisterCloseOnEscape(kEditSubscriptionsWindowTitle);
    APIDefs->GUI_DeregisterCloseOnEscape(kLiveEventReportsWindowTitle);
    APIDefs->GUI_DeregisterCloseOnEscape(kWsDebugWindowTitle);

    //_ Bounded wait so a still-running thread can't resume in unloaded memory; 2s is generous headroom, not a timeout budget.
    WaitForBackgroundThreads(2000);

    //_ Explicit unbounded join for this one long-lived thread (see ws_client.h); after the call above, so its shutdown hook fires first and this join returns quickly.
    ShutdownWsClient();

    //_ After ShutdownWsClient() so its own join is logged too, keeping the log continuous from first connect to this addon's last line (see ws_debug_log.h).
    ShutdownWsDebugLog();

    SaveSettings(g_AddonDir);
    SaveAllData(g_AddonDir);

    //_ Force heap frees now while the CRT is still intact, not left to the static destructor at DLL unload.
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
// Per-frame render callback (RT_Render). Subscriptions views render whenever
// gameplay is active; map-only overlays (world markers, cyclic rings)
// additionally require the full-screen map to be open - see the early-outs below
// for exactly where that split happens.
//--------------------------------------------------------------------------------
void AddonRender()
{
    //_ No-op unless ShowDebug (see ScopedRenderTimer in addon.h); wraps this whole function, every early-return too.
    RenderTimer renderTimer;

    //_ Unlike the map overlays below, this watchlist is a normal ImGui window - renders on IsGameplay; overlays need IsMapOpen too.
    if (MumbleLink && NexusLink && NexusLink->IsGameplay)
    {
        //_ Per-view kill-switches so at least one can stay on; also skips PollGw2Api once none would consume its data.
        bool isCompetitive = MumbleLink->Context.IsCompetitive;
        bool allDisabled = DisableWindowWhenCompetitive && DisableBarWhenCompetitive && DisableNotifyWhenCompetitive;

        if (!(isCompetitive && allDisabled))
        {
            //_ Cheap no-op most frames (internal rate limiting) - called alongside the two views that consume its data.
            PollGw2Api();

            if (!(isCompetitive && DisableWindowWhenCompetitive)) RenderSubscriptionsWindow();
            if (!(isCompetitive && DisableBarWhenCompetitive))    RenderSubscriptionsBar();
            if (!(isCompetitive && DisableNotifyWhenCompetitive)) RenderSubscriptionsNotifications();
        }

        //_ Not gated by the competitive kill-switches above: those govern passive overlay visibility, not an editor the user just explicitly opened.
        RenderEditSubscriptionsWindow();

        //_ Also not gated by the kill-switches: g_LiveEvents only has PvE-map entries, so IsPlayerNearLiveEvent is already false on PvP/WvW (events_live.h).
        RenderLiveEventButtons();
        RenderLiveEventReportsWindow();
    }

    if (!MumbleLink || !NexusLink)          return;
    if (!NexusLink->IsGameplay)             return;

    //_ Edit mode (see maprender.h) must not stay armed once the map closes, or a later reopen resumes dragging with no visual cue; needs the FALLING edge.
    static bool wasMapOpen = false;
    bool isMapOpen = MumbleLink->Context.IsMapOpen;
    if (wasMapOpen && !isMapOpen)
        ClearEditMode();
    wasMapOpen = isMapOpen;

    if (!isMapOpen) return;

    //_ PvP/WvW maps never have Basic/Cyclic events on them - unconditional, not tied to any setting like the views above.
    if (MumbleLink->Context.IsCompetitive) return;

    RenderMapEvents();
    if (ShowCyclicOverlay)
        RenderCyclicGroups();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetAddonDef
//--------------------------------------------------------------------------------
// Required Nexus export; returns the static AddonDefinition_t Nexus reads once at
// load to get metadata and the Load/Unload function pointers.
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