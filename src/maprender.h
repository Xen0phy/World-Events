#pragma once
#include "imgui.h"

// Convert a GW2 continent coordinate to a screen pixel position,
// using the current Mumble compass state.
ImVec2 ContinentToScreen(float cx, float cy);

// Draw all events from g_Events onto the open world map.
void RenderMapEvents();
