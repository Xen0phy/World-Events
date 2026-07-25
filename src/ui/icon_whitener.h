//################################################################################
// icon_whitener.h
//--------------------------------------------------------------------------------
// Small utility popup that converts a user-selected icon from the textures/
// folder into a gray-channel image suitable for use as a map-overlay icon.
//
// Map icons are tinted at draw time (see maprender.cpp / AddImage's `col`
// parameter), which only works correctly when the source PNG carries its
// shape as a neutral-gray RGB + alpha channel. Full-color icons tint
// unpredictably instead of cleanly changing hue. This tool does the
// conversion in-place so users don't need an external image editor.
//
// Single-pass conversion (see ProcessPixels in icon_whitener.cpp):
// desaturate to luminance (ITU-R BT.709, with proper sRGB linearization -
// matches GIMP's Colors -> Desaturate -> Luminance), then normalize so the
// brightest pixel becomes white, preserving relative shading between light
// and dark areas.
//
// The result is saved as "<textures dir>/<original name>_white.png".
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawIconWhitenerButton / DrawIconWhitenerPopup
//--------------------------------------------------------------------------------
// Call DrawIconWhitenerButton() somewhere in AddonOptions() to add the
// "Whitener" button; it opens the popup managed here. Call
// DrawIconWhitenerPopup() every frame from AddonOptions(), regardless of
// whether the popup is currently open, so it renders while open.
//--------------------------------------------------------------------------------
void DrawIconWhitenerButton();
void DrawIconWhitenerPopup();