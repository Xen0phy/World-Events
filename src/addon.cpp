#include "addon.h"
#include "cyclic.h"
#include "events.h"
#include "maprender.h"
#include "cyclicrender.h"
#include "imgui.h"
#include "version.h"

AddonAPI_t*      APIDefs    = nullptr;
Mumble::Data*    MumbleLink = nullptr;
NexusLinkData_t* NexusLink  = nullptr;

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

    APIDefs->GUI_Register(RT_Render, AddonRender);

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Loaded.");
}

void AddonUnload()
{
    // Deregister first — stop any in-flight render calls
    APIDefs->GUI_Deregister(AddonRender);

    // Force heap frees now while the CRT is still intact,
    // rather than leaving it to the static destructor at DLL unload.
    g_Events.clear();
    g_Events.shrink_to_fit();

    g_CyclicGroups.clear();
    g_CyclicGroups.shrink_to_fit();

    APIDefs->Log(LOGL_INFO, "WorldEvents", "Unloaded.");
}

void AddonRender()
{
    if (!MumbleLink || !NexusLink)          return;
    if (!NexusLink->IsGameplay)             return;
    if (!MumbleLink->Context.IsMapOpen)     return;

    RenderMapEvents();
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
