//################################################################################
// ws_debug_window.h
//--------------------------------------------------------------------------------
// ShowWsDebugWindow          visibility flag - toggle from AddonOptions or
//                            anywhere else convenient
// kWsDebugWindowId           registered with GUI_RegisterCloseOnEscape in
//                            addon.cpp, same pattern as every other window
// RenderWsDebugWindow()      draws the window if ShowWsDebugWindow is set -
//                            see ws_debug_window.cpp for the render-callback details
//--------------------------------------------------------------------------------
// A live view over everything ws_debug_log.h records: every connect attempt,
// every raw message sent or received, every error, from the moment the addon
// loaded to now. Reads straight from ws_debug_log.h's ring buffer and keeps no
// copy of its own - this window is a viewer, not a second source of truth.
//--------------------------------------------------------------------------------

#pragma once

extern bool ShowWsDebugWindow;

//_ ImGui window identity, shared with addon.cpp's GUI_RegisterCloseOnEscape/DeregisterCloseOnEscape -
//_ kept separate from the localized visible title for the same reason as changelog_window.h's
//_ kVersionHistoryWindowId: ImGui hashes a window's ID from only the part of its label after "##",
//_ so this stays stable across languages while RenderWsDebugWindow's titlebar text (Tr("WE_WSDEBUG_TITLE"))
//_ can change with Nexus's active language.
inline constexpr const char* kWsDebugWindowId = "##WorldEventsWsDebugLog";

void RenderWsDebugWindow();