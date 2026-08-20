//################################################################################
// color_utils.h
//--------------------------------------------------------------------------------
// ToImVec4(c)/ToImVec4Opaque(c)   plain float[4] -> ImVec4 copy
// ColorU32(...)                   ImVec4/float[4] -> drawable ImU32
// ColorFloat4(c)                  reverse: ImU32 -> ImVec4, for editing
// ShadeU32(c, factor)             darker/lighter shade, alpha unchanged
// FadeU32(c, alphaMul)            same color, alpha scaled
// ThemeColorU32(styleColor, a)    live Nexus/ImGui theme color, faded
//--------------------------------------------------------------------------------
// Every persisted color in this addon (settings_table.h's SETTING_ARRAY colors,
// and events.json's CyclicGroup::colors.base) is stored natively as RGBA floats
// in [0,1] - the same representation and component order ImGui::ColorEdit3/4 and
// ImGui::ColorConvertFloat4ToU32/U32ToFloat4 already expect, so no bit-unpacking
// is ever needed. Colors used to be stored as packed RRGGBBAA ints instead, which
// forced every call site to hand-roll its own conversion.
//
// idleColor/customColor (CyclicGroup / CyclicGroup::Slot in events.h) are NOT
// part of this - they're already native ImU32, fed straight into ImDrawList
// calls. They still get converted for editing (ColorEdit4 wants floats), just via
// the plain ColorFloat4/ColorU32 pair below, not a bespoke wrapper.
//--------------------------------------------------------------------------------

#pragma once

#include "imgui.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ToImVec4 / ToImVec4Opaque
//--------------------------------------------------------------------------------
// Straight 4-float copy of a SETTING_ARRAY color (stored as plain float[4] so the
// generic array-setting machinery in settings.h/.cpp doesn't need a color-
// specific macro) into ImVec4, for call sites that want member/struct syntax (c.w
// *= alpha, ImGui::TextColored, etc.). ToImVec4Opaque forces alpha to 1.0, for
// the handful of call sites (Subscriptions window's Active/Soon text, the weekly-
// tracked '*' marker) that feed straight into ImGui::TextColored, which has no
// separate opacity control.
//--------------------------------------------------------------------------------
inline ImVec4 ToImVec4(const float c[4])
{
    return ImVec4(c[0], c[1], c[2], c[3]);
}

inline ImVec4 ToImVec4Opaque(const float c[4])
{
    return ImVec4(c[0], c[1], c[2], 1.0f);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ColorU32   (group: ShadeU32, FadeU32)
//--------------------------------------------------------------------------------
// Plain conversion to a drawable ImU32, no adjustment - e.g. ColorSet::pri() in
// events.h.
//--------------------------------------------------------------------------------
inline ImU32 ColorU32(const ImVec4& c)  { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImU32 ColorU32(const float c[4]) { return ColorU32(ToImVec4(c)); }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ColorFloat4
//--------------------------------------------------------------------------------
// Reverse direction, ImU32 -> ImVec4, for ColorEdit4 to edit it as floats.
// idleColor/customColor (events.h) are stored as native ImU32, not one of this
// addon's own RGBA-float settings, so there's no ToImVec4/ColorU32 float-array
// pair for them - this is the one place a stored color needs converting FROM
// ImU32.
//--------------------------------------------------------------------------------
inline ImVec4 ColorFloat4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShadeU32   (group: ColorU32, FadeU32)
//--------------------------------------------------------------------------------
// Scales R/G/B by factor, alpha unchanged - derives a color's secondary/tertiary
// shade (ColorSet::sec()/ter() in events.h).
//--------------------------------------------------------------------------------
inline ImU32 ShadeU32(const ImVec4& c, float factor)
{
    return ColorU32(ImVec4(c.x * factor, c.y * factor, c.z * factor, c.w));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FadeU32   (group: ColorU32, ShadeU32)
//--------------------------------------------------------------------------------
// Multiplies alpha by alphaMul, R/G/B unchanged - the notification toast's fade-
// in/out, the subscription bar's dropping dots/segments, and ThemeColorU32 below
// all animate opacity this same way.
//--------------------------------------------------------------------------------
inline ImU32 FadeU32(const ImVec4& c, float alphaMul)
{
    return ColorU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ThemeColorU32
//--------------------------------------------------------------------------------
// Reads whatever Nexus/the user currently has the shared ImGui context themed to
// (ImGuiCol_WindowBg/ImGuiCol_Text - same context AddonLoad hands off via
// ImGui::SetCurrentContext, see addon.cpp), not a color this addon picks itself.
// alphaMul (0..1) multiplies the style color's own alpha instead of replacing it
// - exactly FadeU32, just starting from a live theme color instead of one of this
// addon's own stored colors.
//--------------------------------------------------------------------------------
inline ImU32 ThemeColorU32(ImGuiCol styleColor, float alphaMul)
{
    return FadeU32(ImGui::GetStyleColorVec4(styleColor), alphaMul);
}