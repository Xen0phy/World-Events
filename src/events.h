#pragma once
#include <vector>
#include <string>

struct WorldEvent
{
    std::string name;       // Event name
    float       continentX; // X coordinate on map
    float       continentY; // Y coordinate on map
    bool        isVarying;  // no periodic schedule
    int         duration;   // seconds the event stays active
    
    // Varying (isVarying = true)
    std::vector<int> varyingTimes; // seconds from UTC midnight, sorted
    
    // Periodic (isVarying = false)
    int         period;     // seconds, e.g. 7200 for 2h events
    int         offset;     // seconds from UTC midnight of first start

    // Optional: filename of a user-supplied icon in the addon's textures/
    // folder, drawn instead of the plain dot when non-empty. The icon is
    // expected to be a desaturated/gray-RGB, alpha-shaped image (see the
    // authoring note near the texture-loading code in maprender.cpp) —
    // it's recolored at draw time via a multiplicative tint to the same
    // gray/orange/red status colors the plain dot already uses, which
    // only works correctly on a neutral-gray source image; a full-color
    // icon would tint unpredictably. Empty string = use the plain dot,
    // which is the default for all existing/compiled-in events. Placed
    // LAST in the struct deliberately, so every existing positional
    // aggregate-initializer in events.cpp keeps working unchanged.
    std::string iconTexture;
};

// All events. Populated in events.cpp, used by maprender.cpp.
extern std::vector<WorldEvent> g_Events;
