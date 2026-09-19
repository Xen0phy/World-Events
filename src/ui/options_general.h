//################################################################################
// options_general.h
//--------------------------------------------------------------------------------
// DrawOptionsGeneral   General tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's first tab: the app-level settings as flat
// CollapsingHeader sections - one nesting level, no sub-tabs. The body currently
// draws only the WE_OPTWIN_SECTION_PENDING line.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsGeneral
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while
// the tab is selected, from inside the content child window.
//--------------------------------------------------------------------------------
void DrawOptionsGeneral();
