#pragma once

#include "Nexus.h"
#include "Mumble.h"
#include <string>

// Global API pointers — set in AddonLoad, valid until AddonUnload
extern AddonAPI_t*      APIDefs;
extern Mumble::Data*    MumbleLink;
extern NexusLinkData_t* NexusLink;

// Addon's own data directory (e.g. "<GW2>/addons/WorldEvents"), set once in
// AddonLoad via APIDefs->Paths_GetAddonDirectory. Used for settings.ini and
// any future JSON data files.
extern std::string g_AddonDir;

// ---------------------------------------------------------------------------
// Debug switch — build-time only, deliberately NOT a settings_table.h
// SETTING. Flip this constant and rebuild rather than exposing it as a
// real user-facing option; it isn't persisted anywhere. Gates the render-
// timing measurement below (see RenderTimer in addon.cpp) and the debug
// line addon_options.cpp shows underneath the release date/time.
// ---------------------------------------------------------------------------
inline constexpr bool ShowDebug = true;

// Rolling average, in milliseconds, of how long AddonRender's own body
// took to run — updated about once a second (see RenderTimer in
// addon.cpp), rather than showing a single frame's noisy raw time. Only
// ever written while ShowDebug is true; left at 0 otherwise.
extern float g_AvgRenderTimeMs;

// Nexus-required exports
void AddonLoad  (AddonAPI_t* aAPI);
void AddonUnload();
void AddonRender();

// Options panel callback (RT_OptionsRender) — defined in addon_options.cpp
void AddonOptions();
