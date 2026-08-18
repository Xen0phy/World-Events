//################################################################################
// maprender.h
//--------------------------------------------------------------------------------
// IsEventActive/GetSecondsUntilEventStart/GetSecondsUntilEventEnd
//                              Basic Event schedule queries, shared with the
//                              subscriptions watchlist window
// ContinentToScreen/ScreenToContinent
//                              continent coordinate <-> screen pixel
// EditModeState/g_EditMode    momentary drag-to-reposition state for the map
// ClearEditMode                clears g_EditMode unconditionally
// GetEventZoomSizeMultiplier  current zoom-based marker size multiplier
// RenderMapEvents              draws all Basic Events onto the open world map
// GetEventIconFilenames/ScanEventIconFiles
//                              icon-picker dropdown contents
//--------------------------------------------------------------------------------

#pragma once

#include "events.h"
#include "imgui.h"
#include "Nexus.h"

#include <ctime>
#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsEventActive / GetSecondsUntilEventStart / GetSecondsUntilEventEnd
//--------------------------------------------------------------------------------
// Basic Event schedule queries, shared with the subscriptions watchlist window so
// it doesn't reimplement the same varying/periodic phase math used to draw
// markers. GetSecondsUntilEventStart returns -1 if the event has no usable
// schedule data (e.g. an empty varyingTimes list), 0 if already active.
// GetSecondsUntilEventEnd is only meaningful while IsEventActive() is true.
//--------------------------------------------------------------------------------
bool IsEventActive(const WorldEvent& ev, time_t now);
int GetSecondsUntilEventStart(const WorldEvent& ev, time_t now);
int GetSecondsUntilEventEnd(const WorldEvent& ev, time_t now);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContinentToScreen / ScreenToContinent
//--------------------------------------------------------------------------------
// Converts between a GW2 continent coordinate and a screen pixel position, using
// the current Mumble compass state. ScreenToContinent is the exact inverse, used
// by map-drag editing (see EditModeState below).
//--------------------------------------------------------------------------------
ImVec2 ContinentToScreen(float cx, float cy);
ImVec2 ScreenToContinent(ImVec2 screenPos);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// EditTarget
//--------------------------------------------------------------------------------
// Which kind of marker, if any, is currently armed for drag-to-reposition - see
// EditModeState below.
//--------------------------------------------------------------------------------
enum class EditTarget { None, BasicEvent, CyclicGroup };

//********************************************************************************
// EditModeState
//--------------------------------------------------------------------------------
// target      which kind of marker is armed, if any
// index       index into g_Events or g_CyclicGroups, per `target`
// isDragging  true only while a drag gesture is actively in progress
//--------------------------------------------------------------------------------
// Clicking "Drag" next to a Basic Event's or Cyclic Group's Location field
// (addon_options.cpp) arms drag-to-reposition for that one marker (target/index);
// dragging it on the map then moves it, and the same button (now "Stop") disarms
// it - there is no separate map gesture to end editing, since right-click doesn't
// reliably reach this overlay and a left-click-to-close would conflict with the
// drag gesture itself. At most one marker is armed at a time. isDragging is true
// only during an actual left-click-drag started on the marker itself; without
// that distinction, any left-drag elsewhere - including the mouse-down from
// "Stop" - would be mistaken for dragging the marker.
//--------------------------------------------------------------------------------
struct EditModeState
{
    EditTarget target     = EditTarget::None;
    int        index      = -1;
    bool       isDragging = false;
};

//_ Single shared instance, defined in maprender.cpp, reused by cyclicrender.cpp.
extern EditModeState g_EditMode;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearEditMode
//--------------------------------------------------------------------------------
// Clears g_EditMode unconditionally. Called from AddonRender when the map
// transitions from open to closed, so edit mode never stays silently armed across
// a close/reopen of the map.
//--------------------------------------------------------------------------------
void ClearEditMode();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetEventZoomSizeMultiplier
//--------------------------------------------------------------------------------
// Returns the current zoom-based size multiplier (1.0 = no change), driven by the
// BasicEventZoomScaling* settings and the current map's Compass.Scale. Shared
// with cyclicrender.cpp so cyclic group rings scale the same way as basic event
// markers do.
//--------------------------------------------------------------------------------
float GetEventZoomSizeMultiplier();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderMapEvents
//--------------------------------------------------------------------------------
// Draws all events from g_Events onto the open world map.
//--------------------------------------------------------------------------------
void RenderMapEvents();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetEventIconFilenames / ScanEventIconFiles
//--------------------------------------------------------------------------------
// Returns the sorted list of PNG/JPG filenames found in the addon's textures/
// folder, for the options panel's icon-picker dropdown. Call ScanEventIconFiles()
// to refresh after the user adds new files - there's no automatic
// filesystem-watching.
//--------------------------------------------------------------------------------
const std::vector<std::string>& GetEventIconFilenames();
void ScanEventIconFiles();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetOrRequestEventIcon
//--------------------------------------------------------------------------------
// Returns the loaded Texture_t* for `filename`, or nullptr if it isn't ready yet
// (this also kicks off an async load the first time a given filename is
// requested). Disk (<addon dir>/textures/filename) is checked first; falling back
// to this addon's bundled default icons only if nothing matches on disk - see
// maprender.cpp for the full scheme. Shared with cyclicrender.cpp's optional
// ring-image overlay so it resolves filenames, caches, and dedupes loads exactly
// like a Basic Event icon does, instead of keeping a second parallel texture
// cache.
//--------------------------------------------------------------------------------
Texture_t* GetOrRequestEventIcon(const std::string& filename);