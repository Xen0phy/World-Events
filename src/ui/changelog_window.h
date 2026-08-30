//################################################################################
// changelog_window.h
//--------------------------------------------------------------------------------
// ShowVersionHistoryWindow        transient visibility flag (see below)
// CheckForVersionHistoryOnLoad    call once from AddonLoad; opens the window if
//                                  this is a new version since the last run
// RenderVersionHistoryWindow      draws the window; no-op unless open
//--------------------------------------------------------------------------------
// World Events' answer to Split Wars' single-string "Installation / Update
// Notice" popup, but structured per-version: kVersionHistory (version_history.h)
// holds one manually-curated entry per release worth mentioning, not every
// release - see version_history.h for why. A dropdown lets the player browse
// older entries too, defaulting to the newest.
//
// Shown at most once per version: LastKnownVersion (settings_table.h) is a
// persisted int compared the same way Split Wars compares its own
// currentVersion - see CheckForVersionHistoryOnLoad. This flag itself always
// starts false; "have we shown this version's notice" lives in LastKnownVersion.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//_ Transient only - see file header. Set true by CheckForVersionHistoryOnLoad, cleared by the window's own "Got it" button or Escape.
extern bool ShowVersionHistoryWindow;

//_ Shared with addon.cpp's APIDefs->GUI_RegisterCloseOnEscape/GUI_DeregisterCloseOnEscape calls.
inline constexpr const char* kVersionHistoryWindowTitle = "World Events — What's New";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CheckForVersionHistoryOnLoad
//--------------------------------------------------------------------------------
// Call once from AddonLoad, after LoadSettings. Compares the compiled Maj/Min/
// Bld/Rev (version.h) against the persisted LastKnownVersion - including a
// brand-new install, where it defaults to 0 - and if they differ, sets
// ShowVersionHistoryWindow and immediately persists the new LastKnownVersion,
// so a crash before a clean unload can't cause the notice to reappear.
//--------------------------------------------------------------------------------
void CheckForVersionHistoryOnLoad(const std::string& addonDir);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderVersionHistoryWindow
//--------------------------------------------------------------------------------
// No-op unless ShowVersionHistoryWindow. Draws a version-picker dropdown
// (kVersionHistory, version_history.h) above the selected entry's notes, newest
// selected by default on every fresh open. "Got it" clears
// ShowVersionHistoryWindow; browsing older entries does not touch
// LastKnownVersion, already persisted by CheckForVersionHistoryOnLoad.
//--------------------------------------------------------------------------------
void RenderVersionHistoryWindow();