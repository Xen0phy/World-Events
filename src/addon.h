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

// Nexus-required exports
void AddonLoad  (AddonAPI_t* aAPI);
void AddonUnload();
void AddonRender();

// Options panel callback (RT_OptionsRender) — defined in addon_options.cpp
void AddonOptions();
