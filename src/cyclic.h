#pragma once
#include <vector>
#include "imgui.h"

struct CyclicGroup
{
    float continentX;
    float continentY;
    int   period;       // seconds per full cycle

    struct Slot
    {
        const char* name;
        int         offset;    // seconds from UTC midnight of first occurrence
        int         duration;  // seconds
        ImU32       color;     // segment color
    };

    std::vector<Slot> slots;
};

// All cyclic groups. Populated in cyclic.cpp, used by cyclicrender.cpp.
extern std::vector<CyclicGroup> g_CyclicGroups;

constexpr ImU32 HEX(unsigned int rrggbbaa, float factor = 1.0f)
{
    return IM_COL32(
        (ImU32)(((rrggbbaa >> 24) & 0xFF) * factor),  // R
        (ImU32)(((rrggbbaa >> 16) & 0xFF) * factor),  // G
        (ImU32)(((rrggbbaa >>  8) & 0xFF) * factor),  // B
          (rrggbbaa        & 0xFF)                     // A unchanged
    );
}

struct ColorSet
{
    unsigned int base;   // RRGGBBAA — source of truth, editable
    ImU32 pri() const { return HEX(base); }
    ImU32 sec() const { return HEX(base, 0.80f); }
    ImU32 ter() const { return HEX(base, 0.60f); }
};
