#pragma once
#include <vector>
#include <optional>
#include <string>
#include <cstdint>
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
    // unset" convention just above. NOT the last field anymore — see
    // `shown` right below for why it sits here instead of at the end.
    std::string chatCode;

    // Shows/hides this event on the MAP OVERLAY only (maprender.cpp skips
    // it entirely when false) — it still shows up in the options panel
    // list either way, and is NOT affected by/doesn't affect the
    // Subscriptions bar/window, which are opt-in (the user has to
    // specifically subscribe to something there regardless of this flag).
    //
    // Defaults to true (shown), not false: this sits right next to a
    // subscribe checkbox in the options panel UI (addon_options.cpp), and
    // two adjacent checkboxes that are BOTH unchecked-by-default/
    // false-by-default look identical at a glance — a user can easily
    // misread "not subscribed, not hidden" as "not subscribed, hidden" or
    // vice versa. `shown` defaulting to true and rendering as a checked
    // checkbox reads unambiguously as "on by default," distinct from the
    // subscribe checkbox next to it starting unchecked.
    //
    // Placed right after chatCode, deliberately NOT at the end after
    // iconTexture — every existing entry in events_basic.cpp already
    // provides something past chatCode (varyingTimes, or period+offset),
    // so putting `shown` at the very end would force every line to also
    // spell out a value for iconTexture just to reach it. Right after
    // chatCode, only one token needs to be inserted per line — and since
    // true is the default, existing compiled-in entries need no token at
    // all; only an event someone actually wants hidden needs
    // `.shown = false` added.
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

        // Shows/hides this SLOT's arc only — the rest of the group's ring
        // (background track + other slots) still draws normally when
        // this is false. For hiding the WHOLE ring, see
        // CyclicGroup::shown below instead.
        //
        // Defaults to true, same reasoning as WorldEvent::shown in this
        // file: this sits next to a per-slot subscribe checkbox in the
        // options panel, and two checkboxes both defaulting to
        // false/unchecked are too easy to misread against each other.
        //
        // Placed right after chatCode for the same reason as
        // WorldEvent::shown: most slots already provide something past
        // chatCode positionally in events_cyclic.cpp (repeat, in the few
        // slots that override it), so this avoids also having to spell
        // out customColor just to reach a trailing field.
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
    // Defaults to true, same reasoning as WorldEvent::shown/Slot::shown
    // above: sits next to a group-level subscribe checkbox in the
    // options panel, and two both-false-by-default checkboxes are too
    // easy to misread against each other at a glance.
    //
    // Placed LAST (after idleColor), unlike WorldEvent::shown/
    // Slot::shown which sit right after chatCode: there's no chatCode
    // at this (whole-group) level to anchor next to, and every existing
    // compiled-in group in events_cyclic.cpp already provides `colors`
    // and `slots` positionally, so inserting this any earlier would
    // force every one of those ~20+ entries to be touched for no
    // reason. Trailing + defaulted to true means none of them need any
    // changes at all; only a group someone actually wants hidden needs
    // `.shown = false` added.
    bool shown = true;

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
// Data version
// ===========================================================================
// A date, as an int (YYYYMMDD), bumped whenever EITHER of these change:
//   - the on-disk SHAPE changes in a way old files can't just fall through
//     defaults for (a field is removed/renamed, not just added — adding a
//     new optional field with a sensible j.value() default does NOT need
//     a bump, since old files keep loading fine and the new field just
//     falls back to its default until the user sets it)
//   - the COMPILED-IN CONTENT changes (a group/event/slot was added,
//     removed, or renamed in events_cyclic.cpp/events_basic.cpp, OR a
//     default category / forced membership changed — see categories.h)
//
// This drives the merge behavior in both LoadEventsData (events_storage.cpp)
// and LoadCategoriesData (categories.cpp): if the saved file's version
// already matches this constant, the file is known to be fully current
// with the compiled-in defaults, so a name present in the defaults but
// missing from the file is treated as something the USER removed/renamed
// — it is NOT resurrected. Resurrection (treating a missing default as new
// shipped content) only happens when this constant is genuinely newer than
// what's saved, i.e. an actual new build with actual new/changed content.
//
// Without this check, renaming something to collide with another existing
// name would cause the old name to come back from the compiled defaults on
// the very next load (looking, to the merge, identical to "a new build
// added this back") while the renamed duplicate also persisted — a real
// bug found and fixed this session. The same principle is why a `forced`
// category membership (see categories.h) only re-asserts itself when this
// version has advanced past what's saved, rather than on every load —
// otherwise a user dragging a forced member elsewhere would see it snap
// back on every single launch instead of just once per actual content
// change.
//
// One constant shared by events/cyclicGroups/categories, since all three
// live in the same events.json file under the same "data_version" key —
// there's one file, so one version number for whether it's current.
//
// int64_t, not int: a plain YYYYMMDD (e.g. 20260705) fits in 32 bits, but
// if this ever grows minute-level precision (YYYYMMDDHHmm, e.g.
// 202607051350) it exceeds INT32_MAX (~2.1 billion) and silently wraps —
// int64_t has headroom for either granularity without revisiting this.
constexpr int64_t EVENTS_DATA_VERSION = 202607051350; // YYYYMMDDHHmm

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
