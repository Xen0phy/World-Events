//################################################################################
// options_help.h
//--------------------------------------------------------------------------------
// DrawOptionsHelp   Help tab content
//--------------------------------------------------------------------------------
// Content pane of the rail's bottom-pinned tab: the version and changelog entry,
// the explanatory text that would otherwise sit inline on other tabs, and the
// diagnostics. The body currently draws only the WE_OPTWIN_SECTION_PENDING line.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsHelp
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected, from inside the content child window.
//--------------------------------------------------------------------------------
void DrawOptionsHelp();
