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
// their categories: Quick rows only subscribe, notify and show on the map; Deep
// rows are the full editors of addon_options_helpers.h. The search box filters
// the list, and a category with a match opens on its own. Deep mode also adds a
// collapsed Categories header above each list: add, rename and delete categories,
// and drop a row onto one to move it there.
//
// Quick/Deep mode and the search text are transient: never saved, and dropped
// whenever the window closes or a deep link arrives. The link's row then opens in
// Quick mode, scrolls to the middle of the tab and flashes.
//--------------------------------------------------------------------------------

#pragma once

#include "options_window.h" //. OptionsDeepLink

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsEvents
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected. link is non-null for exactly one frame after a Basic or Cyclic
// deep link; any other kind is ignored. The section clears the search and
// switches to Quick at once, then keeps the link until the matching sub-tab has
// drawn, since a selected sub-tab shows one frame later. The row is then opened,
// scrolled to and flashed through OptionsHighlight_Set (options_window.h). A
// Cyclic link with a slot id targets that slot; with an empty one, its group.
//--------------------------------------------------------------------------------
void DrawOptionsEvents(const OptionsDeepLink* link);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetOptionsEventsView
//--------------------------------------------------------------------------------
// Sets Quick mode and empties the search box. RenderOptionsWindow calls it on the
// frame after the window closes, so every open starts fresh; DrawOptionsEvents
// calls it for a deep link, so the target row is never filtered out.
//--------------------------------------------------------------------------------
void ResetOptionsEventsView();
