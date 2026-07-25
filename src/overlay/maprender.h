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

#include <ctime>
#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsEventActive / GetSecondsUntilEventStart / GetSecondsUntilEventEnd
//--------------------------------------------------------------------------------
// Basic Event schedule queries, shared with the subscriptions watchlist
// window so it doesn't reimplement the same varying/periodic phase math
// used to draw markers. GetSecondsUntilEventStart returns -1 if the event
// has no usable schedule data (e.g. an empty varyingTimes list), 0 if
// already active. GetSecondsUntilEventEnd is only meaningful while
// IsEventActive() is true.
//--------------------------------------------------------------------------------
bool IsEventActive(const WorldEvent& ev, time_t now);
int GetSecondsUntilEventStart(const WorldEvent& ev, time_t now);
int GetSecondsUntilEventEnd(const WorldEvent& ev, time_t now);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ContinentToScreen / ScreenToContinent
//--------------------------------------------------------------------------------
// Converts between a GW2 continent coordinate and a screen pixel position,
// using the current Mumble compass state. ScreenToContinent is the exact
// inverse, used by map-drag editing (see EditModeState below).
//--------------------------------------------------------------------------------
ImVec2 ContinentToScreen(float cx, float cy);
ImVec2 ScreenToContinent(ImVec2 screenPos);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// EditTarget
//--------------------------------------------------------------------------------
// Which kind of marker, if any, is currently armed for drag-to-reposition -
// see EditModeState below.
//--------------------------------------------------------------------------------
enum class EditTarget { None, BasicEvent, CyclicGroup };

//********************************************************************************
// EditModeState
//--------------------------------------------------------------------------------
// target      which kind of marker is armed, if any
// index       index into g_Events or g_CyclicGroups, per `target`
// isDragging  true only while a drag gesture is actively in progress
//--------------------------------------------------------------------------------
// Clicking the "Drag" button next to a Basic Event's or Cyclic Group's
// Location field (in addon_options.cpp) arms drag-to-reposition for that
// one marker (target/index); left-click-dragging it on the map then moves
// it. The same button (now reading "Stop") disarms it - there's no
// separate gesture on the map itself to end editing, since right-click
// doesn't reliably reach this overlay and a left-click-to-close would
// conflict with left-click being the drag gesture itself. At most one
// marker is ever being edited at a time - arming one always disarms the
// other.
//
// isDragging is not the same as being armed: it's true only while an
// actual left-click-drag gesture is in progress ON THE MARKER itself
// (mouse-down started while hovering it). Without this distinction, ANY
// left-drag anywhere on screen (e.g. dragging the settings window, or the
// mouse-down from clicking "Stop") would be mistaken for dragging the
// marker and yank it to wherever that unrelated drag/click happened to be.
//
// Deliberately NOT in settings_table.h: this is momentary UI state, not a
// persisted preference, identical in spirit to the search query in
// addon_options.cpp.
//--------------------------------------------------------------------------------
struct EditModeState
{
    EditTarget target     = EditTarget::None;
    int        index      = -1;
    bool       isDragging = false;
};

//_ The single shared instance - defined in maprender.cpp, used by both
// maprender.cpp and cyclicrender.cpp.
extern EditModeState g_EditMode;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearEditMode
//--------------------------------------------------------------------------------
// Clears g_EditMode unconditionally. Called from AddonRender when the map
// transitions from open to closed, so edit mode never stays silently armed
// across a close/reopen of the map.
//--------------------------------------------------------------------------------
void ClearEditMode();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetEventZoomSizeMultiplier
//--------------------------------------------------------------------------------
// Returns the current zoom-based size multiplier (1.0 = no change), driven
// by the BasicEventZoomScaling* settings and the current map's
// Compass.Scale. Shared with cyclicrender.cpp so cyclic group rings scale
// the same way as basic event markers do.
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
// Returns the sorted list of PNG/JPG filenames found in the addon's
// textures/ folder, for the options panel's icon-picker dropdown. Call
// ScanEventIconFiles() to refresh after the user adds new files - there's
// no automatic filesystem-watching.
//--------------------------------------------------------------------------------
const std::vector<std::string>& GetEventIconFilenames();
void ScanEventIconFiles();