//################################################################################
// texture_whitener.h
//--------------------------------------------------------------------------------
// Small utility popup that converts a user-selected icon or texture from the
// textures/ folder into a gray-channel image suitable for use as a map-overlay
// icon or a cyclic-ring texture.
//
// Map icons are tinted at draw time (see maprender.cpp / AddImage's `col`
// parameter), which only works correctly when the source PNG carries its shape as
// a neutral-gray RGB + alpha channel. Full-color images tint unpredictably
// instead of cleanly changing hue. This tool does the conversion in-place so
// users don't need an external image editor.
//
// Single-pass conversion (see ProcessPixels in texture_whitener.cpp): desaturate
// to luminance (ITU-R BT.709, with proper sRGB linearization - matches GIMP's
// Colors -> Desaturate -> Luminance), then normalize so the brightest pixel
// becomes white, preserving relative shading between light and dark areas.
//
// The result is saved as "<textures dir>/<original name>_white.png".
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawTextureWhitenerButton / DrawTextureWhitenerPopup
//--------------------------------------------------------------------------------
// Call DrawTextureWhitenerButton() to add the "Texture Whitener" button (drawn by
// DrawSharedSettings in options_events.cpp); it opens the popup managed here.
// Call DrawTextureWhitenerPopup() right after it, every frame the button is
// drawn, whether or not the popup is currently open, so it renders while open.
//--------------------------------------------------------------------------------
void DrawTextureWhitenerButton();
void DrawTextureWhitenerPopup();