//################################################################################
// options_general.h
//--------------------------------------------------------------------------------
// DrawOptionsGeneral         General tab content
// RequestOpenAccountHeader   open and scroll to the Account and tracking header
//--------------------------------------------------------------------------------
// Content pane of the rail's first tab: the app-level settings as flat
// CollapsingHeader sections - one nesting level, no sub-tabs. Every header starts
// collapsed. Sections, in order: Competitive mode, Subscriptions window,
// Subscriptions bar, Toast popups, Chat and paste, Account and tracking.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsGeneral
//--------------------------------------------------------------------------------
// Called by RenderOptionsWindow (options_window.cpp) once per frame while the tab
// is selected, from inside the content child window.
//--------------------------------------------------------------------------------
void DrawOptionsGeneral();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RequestOpenAccountHeader
//--------------------------------------------------------------------------------
// The next DrawOptionsGeneral call opens the Account and tracking header and
// scrolls it into view, once; a header the player then collapses stays collapsed.
// Pair with OpenOptionsWindow(OptionsTab::General) to send the player to the API
// key field.
//--------------------------------------------------------------------------------
void RequestOpenAccountHeader();