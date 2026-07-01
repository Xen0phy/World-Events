#pragma once
#include "imgui.h"
#include "events.h"
#include <ctime>
#include <string>
#include <vector>

// Returns true if this Basic Event is currently active. Shared with the
// subscriptions watchlist window so it doesn't reimplement the same
// varying/periodic phase math already used to draw markers.
bool IsEventActive(const WorldEvent& ev, time_t now);

// Seconds until this Basic Event's next start (0 if active right now), or
// -1 if the event has no usable schedule data (e.g. an empty varyingTimes
// list). Shared with the subscriptions watchlist window — see IsEventActive.
int GetSecondsUntilEventStart(const WorldEvent& ev, time_t now);

// Seconds until the currently-active window closes. Only meaningful when
// IsEventActive() is true. Shared with the subscriptions watchlist window.
int GetSecondsUntilEventEnd(const WorldEvent& ev, time_t now);

// Convert a GW2 continent coordinate to a screen pixel position,
// using the current Mumble compass state.
ImVec2 ContinentToScreen(float cx, float cy);

// Returns the current zoom-based size multiplier (1.0 = no change), driven
// by the BasicEventZoomScaling* settings and the current map's Compass.Scale.
// Shared with cyclicrender.cpp so cyclic group rings scale the same way as
// basic event markers do.
float GetEventZoomSizeMultiplier();

// Draw all events from g_Events onto the open world map.
void RenderMapEvents();

// Returns the sorted list of PNG/JPG filenames found in the addon's
// textures/ folder, for the options panel's icon-picker dropdown. Call
// ScanEventIconFiles() to refresh after the user adds new files — there's
// no automatic filesystem-watching.
const std::vector<std::string>& GetEventIconFilenames();
void ScanEventIconFiles();
