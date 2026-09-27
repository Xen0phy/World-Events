//################################################################################
// cyclicrender.h   (see: cyclicrender.cpp)
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderCyclicGroups
//--------------------------------------------------------------------------------
// Draws all cyclic group arcs from g_CyclicGroups onto the open world map.
// competitive restricts drawing to fixedToScreen groups only - see
// RenderMapEvents (maprender.h) and AddonRender (addon.cpp).
//--------------------------------------------------------------------------------
void RenderCyclicGroups(bool competitive);