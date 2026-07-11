#pragma once
#include <vector>
#include <optional>
#include <string>
#include <cstdint>
#include "imgui.h"

// ===========================================================================
// Data version
// ===========================================================================
// A date, as an int (YYYYMMDD or YYYYMMDDHHmm), bumped whenever EITHER of
// these change:
//   - the on-disk SHAPE changes in a way old files can't just fall through
//     defaults for (a field is removed/renamed — adding a new optional
//     field with a sensible j.value() default does NOT need a bump)
//   - the COMPILED-IN CONTENT changes (a group/event/slot was added,
//     removed, or renamed in events_cyclic.cpp/events_basic.cpp, OR a
//     default category / forced membership changed — see events_categories.h)
//
// Drives the merge behavior in LoadEventsData (events_storage.cpp) and
// LoadCategoriesData (events_categories.cpp) — see MergeByKey's comment
// there for what this gates and why.
//
// One constant shared by events/cyclicGroups/categories, since all three
// live in the same events.json file under the same "data_version" key.
//
// int64_t, not int: YYYYMMDD fits in 32 bits, but YYYYMMDDHHmm (e.g.
// 202607051350) exceeds INT32_MAX and would silently wrap.
constexpr int64_t EVENTS_DATA_VERSION = 202607060140; // YYYYMMDDHHmm

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
    // unset" convention just above.
    std::string chatCode;

    // Shows/hides this event on the MAP OVERLAY only (maprender.cpp skips
    // it entirely when false) — it still shows up in the options panel
    // list either way, and doesn't affect the Subscriptions bar/window,
    // which are opt-in separately. Defaults to true so it reads clearly
    // as "on by default" next to the subscribe checkbox in the options
    // panel, which starts unchecked.
    //
    // Placed right after chatCode rather than at the end (after
    // iconTexture): this struct uses positional aggregate initialization
    // (see events_basic.cpp), so every existing compiled-in entry already
    // has values past chatCode — putting `shown` after iconTexture would
    // force every line in events_basic.cpp to also specify iconTexture
    // just to reach it.
    bool shown = true;

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
    
    // Varying (isVarying = true)
    std::vector<int> varyingTimes; // seconds from UTC midnight, sorted
    
    // Periodic (isVarying = false)
    int         period;     // seconds, e.g. 7200 for 2h events
    int         offset;     // seconds from UTC midnight of first start

    // Cross-reference into the PUBLIC GW2 API's /v2/worldbosses id list
    // (e.g. "tequatl_the_sunless"), used to ask /v2/account/worldbosses
    // (see gw2_api.h) whether the account has already killed this boss
    // since the last daily reset. Empty = no equivalent — most entries in
    // this file (invasions, LLA, fractal incursions, convergences) have
    // NO API-visible "done today" signal at all, since the public API
    // only exposes this for the 13 classic Tyria world bosses. Leaving it
    // empty is the correct/only option for those, not a TODO.
    //
    // Deliberately LAST: every existing row in events_basic.cpp already
    // provides all 11 fields above positionally, so adding this at the
    // end means only the 13 rows that actually have an API id need a
    // 12th value appended — everything else keeps compiling unchanged
    // and defaults to "" via aggregate init.
    std::string apiWorldBossId;
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
        // unset. Same field as WorldEvent::chatCode on the Basic Event
        // side. Placed here, right after `tier`, rather than at the very
        // end, so a compiled-in entry can add just a chat code without
        // also having to backfill `repeat`/`customColor` with their
        // defaults.
        std::string chatCode;

        // Shows/hides this SLOT's arc only — the rest of the group's ring
        // (background track + other slots) still draws normally when
        // this is false. For hiding the WHOLE ring, see
        // CyclicGroup::shown below instead.
        //
        // Defaults to true and sits right after chatCode, same as
        // WorldEvent::shown.
        bool        shown = true;

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

    // Shows/hides the ENTIRE ring for this group — background track AND
    // every slot — as if the group didn't exist this frame, when false.
    // For hiding just one occurrence within an otherwise-visible ring,
    // see Slot::shown above instead.
    //
    // Defaults to true, same as WorldEvent::shown/Slot::shown.
    bool shown = true;

    // Cross-reference into the PUBLIC GW2 API's /v2/mapchests id list
    // (e.g. "auric_basin_heros_choice_chest"), used to ask
    // /v2/account/mapchests (see gw2_api.h) whether the account has
    // already claimed this map's Hero's Choice Chest since the last
    // daily reset. Empty = no equivalent.
    //
    // GROUP-LEVEL, not per-slot, unlike WorldEvent::apiWorldBossId being
    // per-event: every one of the 8 maps /v2/account/mapchests actually
    // covers grants its single chest from whichever slot in this
    // addon's data represents that map's climactic meta step (or, for
    // The Desolation/Domain of Vabbi, from more than one slot sharing
    // one daily limit) — so "done today" is a property of the whole
    // ring, not of one slot in isolation. Checked once per group in
    // subscriptions_window.cpp/subscriptions_bar.cpp's slot loop, same
    // place the Basic Event apiWorldBossId check already lives.
    //
    // Every other CyclicGroup (invasions, LLA, fractal incursions,
    // convergences, and every map meta /v2/mapchests doesn't cover) has
    // NO API-visible "done today" signal at all and simply leaves this
    // empty — not a TODO, the correct/only option for those.
    std::string apiMapChestId;

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
