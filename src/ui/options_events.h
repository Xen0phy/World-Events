//################################################################################
// options_events.h
//--------------------------------------------------------------------------------
// DrawOptionsEvents        Events tab content
// ResetOptionsEventsView   back to Quick mode with an empty search box
//--------------------------------------------------------------------------------
// Content pane of the rail's second tab: the top strip (search box, Quick/Deep
// toggle, Reset and Restore buttons, right-click hint), the Shared settings
// header (zoom scaling, Texture Whitener), and the Basic/Cyclic sub-tabs - the
// one tab with a second nesting level. Each tab opens with its own settings
// header. Every header starts collapsed, in Quick and Deep alike. Under each
// tab's settings header, the events (Basic) or groups (Cyclic) are listed in
// their categories, drawn by the row drawers of options_events_rows.h. The toggle
// changes only what an expanded row shows: the notify level and Done for today
// (Subscribe all for a group) in both modes, every editing field in Deep. Rows
// have the right-click menu and drag into categories in both modes. The search
// box filters the list, and a category with a match opens on its own. A toolbar
// above each list adds events (Basic) or groups (Cyclic) and categories; category
// rows rename, delete and take dropped rows, in Quick and Deep alike. Adding an
// event or group switches to Deep, where the new entry's fields are.
//
// Quick/Deep mode and the search text are transient: never saved, and dropped
// whenever the window closes. A deep link only empties the search.
//--------------------------------------------------------------------------------

#pragma once

#include "options_window.h" //. OptionsDeepLink

//_ static ###-IDs sor ImGUI persistence when changing language
inline constexpr const char* kDrawSharedSettingsId = "###DrawSharedSettings";
inline constexpr const char* kDrawBasicSettingsId = "###DrawBasicSettings";
inline constexpr const char* kDrawCyclicSettingsId = "###DrawCyclicSettings";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsEvents
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected. link is non-null for exactly one frame after a Basic or Cyclic
// deep link; any other kind is ignored. The section empties the search box at
// once, so the row is never filtered out, and leaves the Quick/Deep mode as it
// is; it then keeps the link until the matching sub-tab has drawn, since a
// selected sub-tab shows one frame later. The row is then opened, scrolled to and
// flashed through OptionsHighlight_Set (options_window.h). A Cyclic link with a
// slot id targets that slot; with an empty one, its group.
//--------------------------------------------------------------------------------
void DrawOptionsEvents(const OptionsDeepLink* link);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetOptionsEventsView
//--------------------------------------------------------------------------------
// Sets Quick mode and empties the search box. RenderOptionsWindow calls it on the
// frame after the window closes, so every open starts fresh.
//--------------------------------------------------------------------------------
void ResetOptionsEventsView();
