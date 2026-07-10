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

// Inverse of ContinentToScreen — converts a screen pixel position back to
// a continent coordinate. Used by map-drag editing (see EditMode below).
ImVec2 ScreenToContinent(ImVec2 screenPos);

// ---------------------------------------------------------------------------
// Map-drag edit mode
// ---------------------------------------------------------------------------
// Clicking the "Drag" button next to a Basic Event's or Cyclic Group's
// Location field (in addon_options.cpp) arms drag-to-reposition for that
// one marker; left-click-dragging it on the map then moves it. The button
// (now reading "Stop") arms/disarms it — there's no separate gesture on
// the map itself to end editing, since right-click wasn't reliably
// reaching this overlay in testing (something upstream of us appears to
// already consume it) and a left-click-to-close would conflict with
// left-click being the drag button itself. At most one marker is ever
// being edited at a time, and it can be EITHER a Basic Event OR a Cyclic
// Group, never both — arming one always disarms the other.
//
// Deliberately NOT in settings_table.h: this is momentary UI state, not a
// persisted preference, identical in spirit to the search query in
// addon_options.cpp.
// ---------------------------------------------------------------------------
enum class EditTarget { None, BasicEvent, CyclicGroup };

struct EditModeState
{
    EditTarget target     = EditTarget::None;
    int        index      = -1;    // index into g_Events or g_CyclicGroups, per `target`

    // True only while an actual left-click-drag gesture is in progress ON
    // THE MARKER itself (mouse-down started while hovering it). Being
    // "armed" (target/index set) is not the same as actively dragging —
    // without this distinction, ANY left-drag anywhere on screen (e.g.
    // dragging the settings window, or the mouse-down from clicking the
    // "Stop" button) was mistaken for dragging the marker and yanked it
    // to wherever that unrelated drag/click happened to be.
    bool       isDragging = false;
};

// The single shared instance — defined in maprender.cpp, used by both
// maprender.cpp and cyclicrender.cpp.
extern EditModeState g_EditMode;

// Clears g_EditMode unconditionally. Called from AddonRender when the map
// transitions from open to closed, so edit mode never stays silently armed
// across a close/reopen of the map.
void ClearEditMode();

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
