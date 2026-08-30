//################################################################################
// ws_debug_window.h
//--------------------------------------------------------------------------------
// ShowWsDebugWindow          visibility flag - toggle from AddonOptions or
//                            anywhere else convenient
// kWsDebugWindowTitle        registered with GUI_RegisterCloseOnEscape in
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

inline constexpr const char* kWsDebugWindowTitle = "World Events — WS Debug Log";

void RenderWsDebugWindow();