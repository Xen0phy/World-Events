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
};

// All events. Populated in events.cpp, used by maprender.cpp.
extern std::vector<WorldEvent> g_Events;
