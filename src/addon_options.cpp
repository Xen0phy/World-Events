// addon_options.cpp
// Implements the World Events section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns, not
// a standalone window. All widgets write directly into the global settings
// declared in settings.h / settings_table.h. There is no explicit "Save"
// button: settings are written to disk on AddonUnload (see addon.cpp), the
// same way your other addons handle it, so edits here just live in memory
// until the addon (or the game) closes.
//
// This first pass only covers the flat scalar settings (overlay visibility,
// ring radius/thickness, entry/exit window). Editing individual cyclic
// groups/slots/events is a separate, later piece once JSON persistence for
// g_CyclicGroups/g_Events exists.

#include "build_info.h"
#include "settings.h"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled / PushItemFlag — not in the public header

// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/
// EndDisabled in this version). Usage: DisabledBlock(cond) { ...widgets... }
// While `cond` is true, widgets inside the block are non-interactive and
// drawn at half alpha. The pop happens automatically when the block ends
// (braces, return, break — anything), so there's no EndDisabled() call to
// forget.
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond) : active(cond)
    {
        if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    }
    ~ImGuiScopedDisabled()
    {
        if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
    }
    explicit operator bool() const { return true; }
};

#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the World Events section inside the Nexus options panel.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    ImGui::TextUnformatted("World Events");
    ImGui::Separator();

    ImGui::Checkbox("Show cyclic event overlay", &ShowCyclicOverlay);

    // Everything below only makes sense while the overlay itself is on —
    // dim and disable it otherwise rather than hiding it outright, so the
    // user can still see what these controls are without losing their
    // place when toggling the overlay off and back on.
    DisabledBlock(!ShowCyclicOverlay)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Ring appearance");
        ImGui::SliderFloat("Radius",    &CyclicRadius,    5.0f, 50.0f, "%.0f px");
        if ( CyclicRadius < CyclicThickness / 2 ) { CyclicThickness = CyclicRadius * 2; }
        ImGui::SliderFloat("Thickness", &CyclicThickness, 5.0f, 100.0f, "%.0f px");
        if ( CyclicThickness > CyclicRadius * 2 ) { CyclicRadius = CyclicThickness / 2; }

        ImGui::Spacing();
        ImGui::TextUnformatted("Entry / exit window");
        ImGui::TextWrapped(
            "How far ahead an upcoming event starts fading into view, and how "
            "long a finished event lingers before fading out. Measured in "
            "degrees of the ring so it scales correctly regardless of how long "
            "a given cycle runs.");
        ImGui::SliderFloat("Future window", &CyclicMaxFutureDeg, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Past window",   &CyclicMaxPastDeg,   0.0f, 360.0f, "%.0f deg");

        if (CyclicMaxFutureDeg + CyclicMaxPastDeg > 360.0f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "Future + past window exceeds 360°, so the ring no longer has "
                "an idle gap — arcs will overlap.");
        }
    }
}
