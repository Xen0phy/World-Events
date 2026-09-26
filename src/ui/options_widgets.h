//################################################################################
// options_widgets.h
//--------------------------------------------------------------------------------
// kSwatchFlags           color swatch flags: button only, hue-wheel picker
// kAlphaSwatchFlags      kSwatchFlags with an alpha bar
// kWarningColor          amber text color for warnings
// ImGuiScopedDisabled    scope that disables and dims the widgets drawn inside it
// DisabledBlock          DisabledBlock(cond) { ... }, the scope as a statement
// SubToggleIndentWidth   indent that lines a row up under a checkbox label
// SubToggleIndent        scope that indents by that width and undoes it on exit
// ImGuiScopedGroupBox    scope that draws a titled border around the widgets inside it
// GroupBox               GroupBox(title) { ... }, the scope as a statement
// SeparatorText          line with a label in it, like ImGui 1.89's SeparatorText
// Tooltip                plain-text tooltip that waits before it shows
// DrawSubscribeCheckbox  checkbox tightened to sit beside a TreeNode arrow
// DrawFileCombo          combo over a file list with a leading empty entry
// DrawBellIcon           hand-drawn bell glyph
// DrawSpeakerIcon        hand-drawn speaker glyph
//--------------------------------------------------------------------------------
// Widgets, colors and layout scopes shared by more than one part of the World
// Events settings window. Pure ImGui code: nothing here reads game state, so any
// tab, and the Help and Reset dialogs, can include this header alone.
//--------------------------------------------------------------------------------

#pragma once

#include "imgui.h"

#include <string>
#include <vector>

//_ Swatch button only, no numeric fields; the click opens a hue-wheel picker.
inline constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;

//_ kSwatchFlags plus an alpha bar in the picker, for RGBA colors.
inline constexpr ImGuiColorEditFlags kAlphaSwatchFlags = ImGuiColorEditFlags_AlphaBar | kSwatchFlags;

//_ Amber, for warning text.
inline const ImVec4 kWarningColor(1.0f, 0.6f, 0.2f, 1.0f);

//********************************************************************************
// ImGuiScopedDisabled
//--------------------------------------------------------------------------------
// active   true while the guarded block should be non-interactive/dimmed
//--------------------------------------------------------------------------------
// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/
// EndDisabled in this version). While `active` is true, widgets drawn inside the
// scope are non-interactive and drawn at half alpha; both effects are popped
// automatically on destruction, so there's no EndDisabled() call to forget.
// Driven through the DisabledBlock macro below, not constructed directly.
//--------------------------------------------------------------------------------
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond);
    ~ImGuiScopedDisabled();
    explicit operator bool() const { return true; }
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DisabledBlock
//--------------------------------------------------------------------------------
// Usage: DisabledBlock(cond) { ...widgets... }. Wraps the block in an
// ImGuiScopedDisabled whose lifetime is the if-statement's scope, so the
// disable/dim state clears on any exit from the block (braces, return, break)
// without a matching end call. DISABLED_BLOCK_CONCAT_/CONCAT exist only to give
// each expansion a unique per-line variable name.
//--------------------------------------------------------------------------------
#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubToggleIndentWidth
//--------------------------------------------------------------------------------
// The distance that lines a row up under a checkbox's label: the checkbox square
// plus the item spacing. Read from the current style, so it follows the UI scale.
//--------------------------------------------------------------------------------
inline float SubToggleIndentWidth()
{
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
}

//********************************************************************************
// SubToggleIndent
//--------------------------------------------------------------------------------
// width   the indent applied, so the destructor undoes exactly that amount
//--------------------------------------------------------------------------------
// Indents by SubToggleIndentWidth from its declaration to the end of the
// enclosing scope, on any exit, so there is no Unindent call to forget. Open a
// nested block to end the indent earlier.
//--------------------------------------------------------------------------------
struct SubToggleIndent
{
    float width;
    SubToggleIndent() : width(SubToggleIndentWidth()) { ImGui::Indent(width); }
    ~SubToggleIndent() { ImGui::Unindent(width); }
};

//********************************************************************************
// ImGuiScopedGroupBox
//--------------------------------------------------------------------------------
// origin        top-left of the title row, screen space
// pad           inner padding on each side, the style's WindowPadding
// right         screen x of the right border
// titleHeight   height of the title row
// titleStartX   screen x where the top border stops before the title
// titleEndX     screen x where the top border resumes after the title
// regionMaxX    ContentRegionRect right edge to restore on exit
// workMaxX      WorkRect right edge to restore on exit
//--------------------------------------------------------------------------------
// A titled frame around the widgets drawn inside it, cut open in the top border
// around the title. Nothing is filled, so it works on any background, in a table
// cell, and under DisabledBlock. Border color is ImGuiCol_Border, corner radius
// ChildRounding, title color ImGuiCol_Text. `width` 0 fills the available width,
// a positive value sets it, a negative value leaves that many pixels free.
// Inside, the content region ends one pad short of the right border, so wrapped
// text, fill-width items and nested boxes stop there; Separator() in ImGui 1.80
// still spans the window. The box is one layout item, so SameLine() puts boxes
// side by side. Title text after "##" is hidden, and the title is pushed as an ID
// scope. Driven through the GroupBox macro below.
//--------------------------------------------------------------------------------
struct ImGuiScopedGroupBox
{
    ImVec2 origin;
    ImVec2 pad;
    float right;
    float titleHeight;
    float titleStartX;
    float titleEndX;
    float regionMaxX;
    float workMaxX;
    ImGuiScopedGroupBox(const char* title, float width = 0.0f);
    ~ImGuiScopedGroupBox();
    explicit operator bool() const { return true; }
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GroupBox
//--------------------------------------------------------------------------------
// Usage: GroupBox(title) { ...widgets... } or GroupBox(title, width) { ... }.
// Same scope trick as DisabledBlock, sharing its DISABLED_BLOCK_CONCAT for the
// per-line variable name. The border is drawn when the block exits, by any path.
//--------------------------------------------------------------------------------
#define GroupBox(...) if (ImGuiScopedGroupBox DISABLED_BLOCK_CONCAT(_group_box_scope_, __LINE__){__VA_ARGS__})

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SeparatorText
//--------------------------------------------------------------------------------
// Horizontal line with a label in it, the look of ImGui::SeparatorText from ImGui
// 1.89 and later, which this ImGui version lacks. A short stub, the label, then a
// line to the right edge of the content region, so inside a GroupBox it stops at
// the box's padding. Label text after "##" is hidden. The lines use
// ImGuiCol_Separator and the label ImGuiCol_Text. An empty label draws a plain
// line. Not interactive. Named like the newer call, so upgrading ImGui is a
// rename to ImGui::SeparatorText.
//--------------------------------------------------------------------------------
void SeparatorText(const char* label);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tooltip
//--------------------------------------------------------------------------------
// Displays a plain-text tooltip for the last ImGui item, but only after the
// cursor has been hovering for delaySeconds (default 0.5s), to keep tooltips from
// flickering up while the player is just moving the mouse across the window.
//--------------------------------------------------------------------------------
void Tooltip(const char* text, float delaySeconds = 0.5f);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribeCheckbox
//--------------------------------------------------------------------------------
// Small "Watch"/"show on map" style checkbox, tightened to match a TreeNode
// arrow's height. Meant to sit immediately before a TreeNode call on the same
// line, giving "[x] > TreeNode". Returns true if toggled this frame, same
// contract as ImGui::Checkbox.
//--------------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawFileCombo
//--------------------------------------------------------------------------------
// A 100 px combo over a folder's file names, with an entry for the empty name
// first (emptyKey, a localization key). Picking it stores an empty string in
// `value`. A stored name missing from `files` shows as the empty entry and stays
// untouched until another entry is picked. `label` is the full ImGui label,
// suffix included.
//--------------------------------------------------------------------------------
void DrawFileCombo(const char* label, const char* emptyKey, const std::vector<std::string>& files, std::string& value);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBellIcon / DrawSpeakerIcon
//--------------------------------------------------------------------------------
// Hand-drawn glyphs (no icon font in the base build); see the .cpp for the
// geometry notes. `center` is the visual center, `size` roughly the full height
// in pixels. DrawSpeakerIcon is also a plain label glyph next to the
// notification-sound picker (options_general.cpp).
//--------------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);
void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color);