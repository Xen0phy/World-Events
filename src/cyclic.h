#pragma once
#include <vector>
#include <optional>
#include "imgui.h"

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

enum class ColorTier { Primary, Secondary, Tertiary };

struct CyclicGroup
{
    const char* name;        // overall name of the cycle, e.g. "Domain of Vabbi"
    float continentX;
    float continentY;
    int   period;       // seconds per full cycle
    ColorSet colors;    // base palette for this group; slots pick a tier from it by default

    struct Slot
    {
        const char* name;
        int         offset;    // seconds from UTC midnight of first occurrence
        int         duration;  // seconds
        ColorTier   tier = ColorTier::Primary; // which of the group's colors.pri()/sec()/ter() to use by default
        int         repeat = 1; // number of evenly-spaced occurrences per period.
                                 // period must be evenly divisible by repeat.

        // Optional per-slot override. If set, this exact color is used
        // instead of deriving one from `tier` — lets a user assign an
        // arbitrary, independent color to any individual slot (e.g. 5
        // different colors for 5 events in one group), while everything
        // that doesn't set this keeps the default shade-derivation behavior.
        std::optional<ImU32> customColor;
    };

    std::vector<Slot> slots;

    ImU32 SlotColor(const Slot& slot) const
    {
        if (slot.customColor.has_value())
            return *slot.customColor;

        switch (slot.tier)
        {
            case ColorTier::Secondary: return colors.sec();
            case ColorTier::Tertiary:  return colors.ter();
            default:                   return colors.pri();
        }
    }
};

// All cyclic groups. Populated in cyclic.cpp, used by cyclicrender.cpp.
extern std::vector<CyclicGroup> g_CyclicGroups;
