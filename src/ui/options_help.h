//################################################################################
// options_help.h
//--------------------------------------------------------------------------------
// DrawOptionsHelp   Help tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's bottom-pinned tab, in two groups: About (version,
// release date, changelog button) and Diagnostics (WS debug window button, plus
// render-time metrics in ShowDebug builds). Draws no settings and keeps no state.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsHelp
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected, from inside the content child window.
//--------------------------------------------------------------------------------
void DrawOptionsHelp();