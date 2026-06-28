#pragma once
#include "imgui.h"
#include <string>
#include <vector>

// Convert a GW2 continent coordinate to a screen pixel position,
// using the current Mumble compass state.
ImVec2 ContinentToScreen(float cx, float cy);

// Draw all events from g_Events onto the open world map.
void RenderMapEvents();

// Returns the sorted list of PNG/JPG filenames found in the addon's
// textures/ folder, for the options panel's icon-picker dropdown. Call
// ScanEventIconFiles() to refresh after the user adds new files — there's
// no automatic filesystem-watching.
const std::vector<std::string>& GetEventIconFilenames();
void ScanEventIconFiles();
