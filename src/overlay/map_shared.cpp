//################################################################################
// map_shared.cpp
//--------------------------------------------------------------------------------
// See map_shared.h for the shared-between-maprender/cyclicrender rationale for
// both functions below.
//--------------------------------------------------------------------------------

#include "map_shared.h"
#include "maprender.h" //. g_EditMode, ScreenToContinent

#include <cmath>
#include <cstdio>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawEditPulseRing
//--------------------------------------------------------------------------------
void DrawEditPulseRing(ImDrawList* dl, ImVec2 center, float hoverRadius)
{
    float pulse     = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 4.0f);
    float ringR     = hoverRadius + 6.0f + pulse * 3.0f;
    ImU32 editColor = IM_COL32(255, 255, 0, (int)(160 + pulse * 80));
    dl->AddCircle(center, ringR, editColor, 0, 2.0f);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDragAnchor
//--------------------------------------------------------------------------------
void DrawDragAnchor(const char* idPrefix, int index, ImVec2 center, float hoverRadius,
    float* outContinentX, float* outContinentY)
{
    char anchorId[48];
    snprintf(anchorId, sizeof(anchorId), "%s_%d", idPrefix, index);

    ImGui::SetNextWindowPos({center.x - hoverRadius, center.y - hoverRadius});
    ImGui::SetNextWindowSize({hoverRadius * 2.0f, hoverRadius * 2.0f});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin(anchorId, nullptr,
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground    |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::InvisibleButton("##we_drag_hit", {hoverRadius * 2.0f, hoverRadius * 2.0f});

    if (ImGui::IsItemActivated())
        g_EditMode.isDragging = true;

    if (g_EditMode.isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        //_ Claims the mouse for the overlay while dragging so Nexus doesn't
        // also forward this left-drag to GW2 itself, which would otherwise
        // pan/rotate the game's own map underneath the marker being moved.
        ImGui::GetIO().WantCaptureMouse = true;

        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 newContinent = ScreenToContinent(mouse);
        *outContinentX = newContinent.x;
        *outContinentY = newContinent.y;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        g_EditMode.isDragging = false;

    ImGui::End();
}