#pragma once

#include "Nexus.h"
#include "Mumble.h"

// Global API pointers — set in AddonLoad, valid until AddonUnload
extern AddonAPI_t*      APIDefs;
extern Mumble::Data*    MumbleLink;
extern NexusLinkData_t* NexusLink;

// Nexus-required exports
void AddonLoad  (AddonAPI_t* aAPI);
void AddonUnload();
void AddonRender();
