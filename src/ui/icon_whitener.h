#pragma once

// ---------------------------------------------------------------------------
// icon_whitener.h
// ---------------------------------------------------------------------------
// Small utility popup that converts a user-selected icon from the textures/
// folder into a gray-channel image suitable for use as a map-overlay icon.
//
// Map icons are tinted at draw time (see maprender.cpp / AddImage's `col`
// parameter), which only works correctly when the source PNG carries its
// shape as a neutral-gray RGB + alpha channel.  Full-color icons tint
// unpredictably instead of cleanly changing hue.  This tool does the
// conversion in-place so users don't need an external image editor.
//
// Two conversion modes (matching GIMP's Hue-Saturation and Overlay modes):
//   HSV Saturation → desaturates a colored image to pure luminance gray.
//   Overlay        → composites the image over a white layer; for images
//                    that are already gray-ish this simply clears any
//                    leftover color cast without blowing out the brights.
//
// The result is saved as "<textures dir>/<original name>_white.png".
//
// Usage:
//   Call DrawIconWhitenerButton() somewhere in AddonOptions() to add the
//   "Whitener" button.  That button opens the popup managed here.
//   Call DrawIconWhitenerPopup() every frame from AddonOptions() so the
//   popup renders when open.
// ---------------------------------------------------------------------------

// Draw the "Open Icon Whitener" button.  Call from AddonOptions().
void DrawIconWhitenerButton();

// Draw the (possibly invisible) popup window.  Must be called every frame
// from AddonOptions() regardless of whether the popup is currently open.
void DrawIconWhitenerPopup();
