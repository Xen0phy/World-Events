//################################################################################
// options_help.h
//--------------------------------------------------------------------------------
// DrawOptionsHelp   Help tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's bottom-pinned tab, in three groups: About (version,
// release date, changelog button), Live events explained (the feature's prose as
// three collapsed headers, so the Live tab stays controls only), and Diagnostics
// (WS debug window button, plus render-time metrics in ShowDebug builds). Draws
// no settings and keeps no state.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsHelp
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected, from inside the content child window.
//--------------------------------------------------------------------------------
void DrawOptionsHelp();