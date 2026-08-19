//################################################################################
// map_shared.h
//--------------------------------------------------------------------------------
// Small pieces of map-overlay drawing/interaction logic that RenderMapEvents
// (maprender.cpp, one marker per Basic Event) and RenderCyclicGroups
// (cyclicrender.cpp, one ring per Cyclic Group) both need in identical form: the
// pulsing "this is the thing being edited" ring, and the invisible drag-anchor
// window that lets the user reposition a marker/ring by left-click-dragging it.
// Pulled out here once both files ended up living in the same overlay/ folder,
// rather than staying duplicated between them.
//--------------------------------------------------------------------------------

#pragma once

#include "imgui.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawEditPulseRing
//--------------------------------------------------------------------------------
// Draws the pulsing yellow ring that marks whichever marker/ring is currently
// armed for drag-to-reposition (see g_EditMode in maprender.h). `hoverRadius` is
// the radius of the marker/ring's own hover rect - the pulse is drawn just
// outside it so it never competes visually with the marker/ring itself.
//--------------------------------------------------------------------------------
void DrawEditPulseRing(ImDrawList* dl, ImVec2 center, float hoverRadius);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDragAnchor
//--------------------------------------------------------------------------------
// Handles the drag-to-reposition gesture for one marker/ring while it's armed
// (isBeingEdited true at the call site). Creates a small, invisible, input-only
// "anchor" window positioned exactly over [center - hoverRadius, center +
// hoverRadius], and - for as long as g_EditMode.isDragging stays true and the
// left mouse button is held - converts the current mouse position to a continent
// coordinate and writes it into *outContinentX / *outContinentY every frame.
//
// `idPrefix` must be unique per CALLER (e.g. "##we_drag_anchor" for Basic Events,
// "##we_drag_anchor_cyclic" for Cyclic Groups) and `index` unique per marker/ring
// within that caller, so ImGui never confuses one frame's anchor window with
// another's.
//--------------------------------------------------------------------------------
void DrawDragAnchor(const char* idPrefix, int index, ImVec2 center, float hoverRadius,
    float* outContinentX, float* outContinentY);