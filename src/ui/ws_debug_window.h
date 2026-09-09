//################################################################################
// ws_debug_window.h
//--------------------------------------------------------------------------------
// ShowWsDebugWindow          visibility flag - toggle from AddonOptions or
//                            anywhere else convenient
// kWsDebugWindowId           stable ImGui window ID, independent of the
//                            localized title (see below)
// RenderWsDebugWindow()      draws the window if ShowWsDebugWindow is set -
//                            see ws_debug_window.cpp for the render-callback details
//--------------------------------------------------------------------------------
// A live view over everything ws_debug_log.h records: every connect attempt,
// every raw message sent or received, every error, from the moment the addon
// loaded to now. Reads straight from ws_debug_log.h's ring buffer and keeps no
// copy of its own - this window is a viewer, not a second source of truth.
//
// kWsDebugWindowId stays suffix-only and stable across languages: Dear ImGui
// hashes a window's ID from only the text after "##", so keeping this constant
// lets addon.cpp's GUI_RegisterCloseOnEscape/DeregisterCloseOnEscape target the
// same window regardless of what RenderWsDebugWindow's titlebar text
// (Tr("WE_WSDEBUG_TITLE")) currently says - same reasoning as
// changelog_window.h's kVersionHistoryWindowId.
//--------------------------------------------------------------------------------

#pragma once

extern bool ShowWsDebugWindow;

//_ See file header for why this ID is suffix-only and stable across languages.
inline constexpr const char* kWsDebugWindowId = "##WorldEventsWsDebugLog";

void RenderWsDebugWindow();