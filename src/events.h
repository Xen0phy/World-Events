#pragma once
#include <vector>
#include <optional>
#include <string>
#include "imgui.h"

// ===========================================================================
// Basic Events
// ===========================================================================

struct WorldEvent
{
    std::string name;       // Event name
    float       continentX; // X coordinate on map
    float       continentY; // Y coordinate on map
    bool        isVarying;  // no periodic schedule
    int         duration;   // seconds the event stays active
    
    // Optional GW2 chat/map code (e.g. "[&BIgIAAA=]" for a waypoint),
    // free text set by the user in the options panel for this event.
    // Empty string = no code set, matching iconTexture's "empty means
    // unset" convention just above. Placed LAST, same reasoning as
    // iconTexture: keeps every existing positional aggregate-initializer
    // in events_basic.cpp working unchanged.
    std::string chatCode;
    
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
    // aggregate-initializer in events_basic.cpp keeps working unchanged.
    std::string iconTexture;
};

// All events. Populated in events_basic.cpp, used by maprender.cpp.
extern std::vector<WorldEvent> g_Events;

// ===========================================================================
// Cyclic Events
// ===========================================================================

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
    std::string name;        // overall name of the cycle, e.g. "Domain of Vabbi"
    float continentX;
    float continentY;
    int   period;       // seconds per full cycle
    ColorSet colors;    // base palette for this group; slots pick a tier from it by default

    struct Slot
    {
        std::string name;
        int         offset;    // seconds from UTC midnight of first occurrence
        int         duration;  // seconds
        ColorTier   tier = ColorTier::Primary; // which of the group's colors.pri()/sec()/ter() to use by default

        // Optional GW2 chat/map code (e.g. "[&BIgIAAA=]") the user can
        // copy to clipboard for this specific slot/occurrence. Empty =
        // unset. See WorldEvent::chatCode in events.h for the same field
        // on the Basic Event side. Placed here (right after `tier`, the
        // last field entries commonly set positionally) rather than at
        // the very end, so a compiled-in entry can add just a chat code
        // without also having to backfill `repeat`/`customColor` with
        // their defaults — see the reorder discussion where this was
        // introduced for why `repeat`/`customColor` still had to move.
        std::string chatCode;

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

    // Optional override for the idle/background track color (the part of
    // the ring where nothing is scheduled). If not set, defaults to
    // colors.ter() — already the dimmest shade of the group's palette by
    // construction, so no separate derivation is needed for the common
    // case. Only needed explicitly when a user wants the idle track to be
    // a genuinely different color than any shade of the group's own palette
    // (e.g. for a single-slot group where ter() might still look too close
    // to the slot's own color for the user's taste).
    std::optional<ImU32> idleColor;

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

    ImU32 IdleColor() const
    {
        return idleColor.has_value() ? *idleColor : colors.ter();
    }
};

// All cyclic groups. Populated in events_cyclic.cpp, used by cyclicrender.cpp.
extern std::vector<CyclicGroup> g_CyclicGroups;

// ===========================================================================
// Time
// ===========================================================================

constexpr int MIN(int minutes) { return minutes * 60; }

constexpr int   m5=MIN(  5),  m10=MIN( 10),  m15=MIN( 15),  m20=MIN( 20),
               m25=MIN( 25),  m30=MIN( 30),  m35=MIN( 35),  m40=MIN( 40),
               m45=MIN( 45),  /* m50 not needed */   m55=MIN( 55),  m60=MIN( 60),
               m65=MIN( 65),  m70=MIN( 70),  m75=MIN( 75),  m80=MIN( 80),
               /* m85 not needed  */  m90=MIN( 90),  m95=MIN( 95), m100=MIN(100),
              m105=MIN(105),  /* m110 not needed */ m115=MIN(115), m120=MIN(120);
