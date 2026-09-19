//################################################################################
// options_live.h
//--------------------------------------------------------------------------------
// DrawOptionsLive   Live tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's third tab: the live-event subscribe switch, the
// display toggles and the single live-event list. The body currently draws only
// the WE_OPTWIN_SECTION_PENDING line.
//--------------------------------------------------------------------------------

#pragma once

#include "options_window.h" //. OptionsDeepLink

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsLive
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while
// the tab is selected. link is non-null for exactly one frame after a Live deep
// link: the section then scrolls to the row and calls OptionsHighlight_Set
// (options_window.h).
//--------------------------------------------------------------------------------
void DrawOptionsLive(const OptionsDeepLink* link);
