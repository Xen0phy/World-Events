#pragma once
#include "imgui.h"

// ---------------------------------------------------------------------------
// color_utils.h
// ---------------------------------------------------------------------------
// Every persisted color in this addon (settings_table.h's SETTING_ARRAY
// colors, and events.json's CyclicGroup::colors.base) is stored natively as
// RGBA floats in [0,1] — the same representation ImGui::ColorEdit3/4 read
// and write directly, and the same component order
// ImGui::ColorConvertFloat4ToU32/U32ToFloat4 already expect.
//
// That's deliberate: colors used to be stored as packed RRGGBBAA unsigned
// ints (R in the high byte — NOT ImGui's own ABGR ImU32 packing), which
// meant every file that needed to draw or edit one had to re-derive its own
// channel-unpacking function (five near-identical copies existed across
// maprender.cpp, addon_options_helpers.cpp, subscriptions_bar.cpp,
// subscriptions_notification.cpp, and subscriptions_window.cpp — see git
// history around 2026-07 for the cleanup). Storing colors as plain floats
// removes the need for any of that: the only conversions ever required are
// the ones ImGui itself already provides.
//
// idleColor/customColor (CyclicGroup / CyclicGroup::Slot in events.h) are
// NOT part of this — they're already real, native ImU32 values (fed
// straight into ImDrawList calls) and always have been. They still get
// converted for editing (ColorEdit4 wants floats), just via the plain
// ColorFloat4/ColorU32 pair below rather than a bespoke wrapper — only the
// RRGGBBAA-packed values ever needed something beyond that.
// ---------------------------------------------------------------------------

// SETTING_ARRAY colors are stored as a plain float[4] (so the generic
// array-setting machinery in settings.h/.cpp doesn't need a color-specific
// macro of its own) — this is a straight 4-float copy into ImVec4 for call
// sites that want member/struct syntax (c.w *= alpha, ImGui::TextColored,
// etc.). No bit math, unlike everything it replaces.
inline ImVec4 ToImVec4(const float c[4])
{
    return ImVec4(c[0], c[1], c[2], c[3]);
}

// Same, but forces alpha to 1.0 — for the handful of call sites (the
// Subscriptions window's Active/Soon text colors, the weekly-tracked '*'
// marker) that feed straight into ImGui::TextColored, which has no
// separate opacity control worth exposing, so alpha was always ignored
// there even back when colors were packed RRGGBBAA.
inline ImVec4 ToImVec4Opaque(const float c[4])
{
    return ImVec4(c[0], c[1], c[2], 1.0f);
}

// ---------------------------------------------------------------------------
// ColorU32 / ShadeU32 / FadeU32
// ---------------------------------------------------------------------------
// Every ImU32 this addon ever draws with comes from exactly one of these
// three small operations on a stored RGBA color — naming them means a call
// site reads as "get the drawable color" / "the darker shade of it" /
// "the same color, faded" rather than re-deriving the ImVec4 math and
// calling ImGui::ColorConvertFloat4ToU32 by hand every time.
// ---------------------------------------------------------------------------

// Plain conversion, no adjustment — e.g. ColorSet::pri() in events.h.
inline ImU32 ColorU32(const ImVec4& c)  { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImU32 ColorU32(const float c[4]) { return ColorU32(ToImVec4(c)); }

// The reverse direction — idleColor/customColor (events.h) are stored as
// native ImU32, not one of this addon's own RGBA-float settings, so
// there's no ToImVec4/ColorU32 float-array pair for them; this is the one
// place a stored color needs converting FROM ImU32 rather than to it,
// for ColorEdit4 to edit it as floats.
inline ImVec4 ColorFloat4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// Scales R/G/B by `factor`, alpha unchanged — derives a color's
// secondary/tertiary shade (ColorSet::sec()/ter() in events.h).
inline ImU32 ShadeU32(const ImVec4& c, float factor)
{
    return ColorU32(ImVec4(c.x * factor, c.y * factor, c.z * factor, c.w));
}

// Multiplies alpha by `alphaMul`, R/G/B unchanged — the notification
// toast's fade-in/out, the subscription bar's dropping dots/segments,
// and ThemeColorU32 below all animate opacity this same way.
inline ImU32 FadeU32(const ImVec4& c, float alphaMul)
{
    return ColorU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul));
}

// ---------------------------------------------------------------------------
// ThemeColorU32
// ---------------------------------------------------------------------------
// Reads whatever Nexus/the user currently has the shared ImGui context
// themed to (ImGuiCol_WindowBg/ImGuiCol_Text — same context AddonLoad hands
// off via ImGui::SetCurrentContext, see addon.cpp), rather than a color
// this addon picks itself. alphaMul (0..1) multiplies the style color's
// OWN alpha rather than replacing it — exactly FadeU32, just starting from
// a live theme color instead of one of this addon's own stored colors.
//
// Previously duplicated verbatim (comment and all) in subscriptions_bar.cpp
// and subscriptions_notification.cpp; shared here instead.
// ---------------------------------------------------------------------------
inline ImU32 ThemeColorU32(ImGuiCol styleColor, float alphaMul)
{
    return FadeU32(ImGui::GetStyleColorVec4(styleColor), alphaMul);
}
