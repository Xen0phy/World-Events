//################################################################################
// options_events.h
//--------------------------------------------------------------------------------
// DrawOptionsEvents   Events tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's second tab: the shared settings block, the
// Quick/Deep toggle and the Basic/Cyclic sub-tabs - the one tab with a second
// nesting level. The body currently draws only the WE_OPTWIN_SECTION_PENDING
// line.
//--------------------------------------------------------------------------------

#pragma once

#include "options_window.h" //. OptionsDeepLink

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsEvents
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while
// the tab is selected. link is non-null for exactly one frame after a Basic or
// Cyclic deep link: the section then selects the matching sub-tab, switches to
// Quick, opens the row and calls OptionsHighlight_Set (options_window.h).
//--------------------------------------------------------------------------------
void DrawOptionsEvents(const OptionsDeepLink* link);
