#pragma once

// ---------------------------------------------------------------------------
// icon_whitener.h
// ---------------------------------------------------------------------------
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
// desaturate to luminance (ITU-R BT.709, with proper sRGB linearization —
// matches GIMP's Colors -> Desaturate -> Luminance), then normalize so the
// brightest pixel becomes white, preserving relative shading between light
// and dark areas.
//
// The result is saved as "<textures dir>/<original name>_white.png".
//
// Usage:
//   Call DrawIconWhitenerButton() somewhere in AddonOptions() to add the
//   "Whitener" button. That button opens the popup managed here.
//   Call DrawIconWhitenerPopup() every frame from AddonOptions() so the
//   popup renders when open.
// ---------------------------------------------------------------------------

// Draw the "Open Icon Whitener" button. Call from AddonOptions().
void DrawIconWhitenerButton();

// Draw the (possibly invisible) popup window. Must be called every frame
// from AddonOptions() regardless of whether the popup is currently open.
void DrawIconWhitenerPopup();
