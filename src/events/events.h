//################################################################################
// events.h
//--------------------------------------------------------------------------------
// EVENTS_DATA_VERSION      shared version gate for events.json (see below)
// WorldEvent / g_Events    one-off "Basic" events and their compiled-in list
// ColorSet / ColorTier     RGBA base color + tier-based shade lookup
// CyclicGroup / g_CyclicGroups   per-map cyclic event rings and their list
// MIN() / m5..m120         minute-to-seconds helpers used by events_basic.cpp/
//                          events_cyclic.cpp
//--------------------------------------------------------------------------------
// Defines the data model for both event systems this addon tracks: one-off "Basic
// Events" (world bosses, invasions, LLA, fractal incursions - single WorldEvent
// entries, drawn as dots on the map) and "Cyclic Events" (per-map metas that
// repeat on a fixed ring, e.g. Auric Basin's Challenges/Octovine/ Pylons -
// CyclicGroup entries, drawn as rings). Compiled-in data lives in
// events_basic.cpp/events_cyclic.cpp; maprender.cpp/cyclicrender.cpp are the
// respective renderers.
//
// EVENTS_DATA_VERSION is a YYYYMMDD(HHmm) int, bumped whenever EITHER the on-disk
// SHAPE changes in a way old files can't fall through defaults for (a field
// removed/renamed - a new optional field with a j.value() default does NOT need a
// bump), OR the COMPILED-IN CONTENT changes (a group/event/ slot added, removed,
// or renamed, or a default category/forced membership changed - see
// events_categories.h). It drives the merge behavior in
// LoadEventsData/LoadCategoriesData (see MergeByKey's comment in
// events_storage.cpp), and is shared by events/cyclicGroups/categories, since all
// three live under one "data_version" key in events.json. int64_t, not int: the
// HHmm-precision form (e.g. 202607051350) exceeds INT32_MAX and would silently
// wrap.
//--------------------------------------------------------------------------------

#pragma once

#include "color_utils.h"
#include "imgui.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

//_ YYYYMMDDHHmm, see file header for what this gates and when to bump it.
constexpr int64_t EVENTS_DATA_VERSION = 202608191234;

//********************************************************************************
// WorldEvent
//--------------------------------------------------------------------------------
// name           display name
// continentX/Y   map coords (continent 1 / Tyria)
// isVarying      true = irregular schedule (see varyingTimes), false = periodic
// duration       seconds the event stays active
// chatCode       optional GW2 chat/map code; empty = unset
// shown          map-overlay visibility only, opt-out (default true);
//                doesn't affect Subscriptions, which are opt-in separately
// iconTexture    optional user icon filename; empty = plain dot; must be
//                neutral-gray RGB+alpha, recolored via tint (maprender.cpp)
// varyingTimes   isVarying only: sorted seconds-from-UTC-midnight starts
// period/offset  isVarying=false only: seconds per cycle / first-start offset
// apiWorldBossId /v2/worldbosses id; empty = no API "done today" signal,
//                unset for all but the 13 classic Tyria world bosses
// doneGroup      shared "done today" key (events_tracking.h); rows sharing
//                a reward (e.g. Ley Line Anomaly) share one value
//--------------------------------------------------------------------------------
// One "Basic Event": a single map dot with its own schedule, either periodic
// (period/offset) or irregular (isVarying + varyingTimes).
//
// chatCode/shown/iconTexture/apiWorldBossId/doneGroup are appended in this exact
// order, last-to-first by how rarely each is set: the list below is built with
// positional aggregate init (events_basic.cpp), so each field's position
// determines how many trailing values a compiled-in row must supply.
//--------------------------------------------------------------------------------
struct WorldEvent
{
    std::string name;
    float       continentX;
    float       continentY;
    bool        isVarying;
    int         duration;
    std::string chatCode;
    bool        shown = true;
    std::string iconTexture;

    std::vector<int> varyingTimes;

    int         period;
    int         offset;

    std::string apiWorldBossId;
    std::string doneGroup;
};

//_ Populated in events_basic.cpp, used by maprender.cpp.
extern std::vector<WorldEvent> g_Events;

//********************************************************************************
// ColorSet
//--------------------------------------------------------------------------------
// base                RGBA in [0,1], source of truth; edit directly via
//                     ImGui::ColorEdit4("Color", &base.x, ...)
// pri()/sec()/ter()   primary/secondary/tertiary shades derived from base
//--------------------------------------------------------------------------------
struct ColorSet
{
    ImVec4 base;

    ImU32 pri() const { return ColorU32(base); }
    ImU32 sec() const { return ShadeU32(base, 0.80f); }
    ImU32 ter() const { return ShadeU32(base, 0.60f); }
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ColorTier
//--------------------------------------------------------------------------------
// Selects a slot's default color: ColorSet::pri()/sec()/ter().
//--------------------------------------------------------------------------------
enum class ColorTier { Primary, Secondary, Tertiary };

//********************************************************************************
// CyclicGroup
//--------------------------------------------------------------------------------
// name              cycle name, e.g. "Domain of Vabbi"
// continentX/Y      map coords (continent 1 / Tyria)
// period            seconds per full cycle
// colors            base palette; slots pick a shade by tier (see ColorTier)
// slots             the group's Slot occurrences (see CyclicGroup::Slot)
// idleColor         optional track-color override; defaults to
//                   colors.ter() (already dimmest) when unset
// shown             hides the ENTIRE ring (track + every slot); see
//                   Slot::shown to hide just one slot's arc instead
// apiMapChestId     /v2/mapchests id, GROUP-level not per-slot; empty =
//                   no API "done today" signal
//--------------------------------------------------------------------------------
// One per-map cyclic ring: a repeating `period`-second cycle containing one or
// more Slots, each occupying a fixed offset/duration within it.
//
// apiMapChestId is checked once per group in subscriptions_window.cpp/
// subscriptions_bar.cpp. Groups without an API-visible signal - LLA, invasions,
// fractal incursions, convergences, and maps mapchests doesn't cover - simply
// leave it empty.
//--------------------------------------------------------------------------------
struct CyclicGroup
{
    std::string name;
    float continentX;
    float continentY;
    int   period;
    ColorSet colors;

    //********************************************************************************
    // Slot
    //--------------------------------------------------------------------------------
    // name          slot/event name
    // offset        seconds from UTC midnight of the first occurrence;
    //               ignored when isVarying is true
    // duration      seconds
    // tier          which of the group's colors.pri()/sec()/ter() to use
    // chatCode      optional GW2 chat/map code; empty = unset
    // shown         hides this slot's arc only; see CyclicGroup::shown for
    //               the whole ring
    // repeat        evenly-spaced occurrences per period (period must
    //               divide evenly by this); ignored when isVarying is true
    // customColor   optional per-slot color override; takes precedence
    //               over tier
    // isVarying     true = irregular schedule (see varyingTimes) instead of
    //               offset+repeat; false (default) = offset+repeat as before
    // varyingTimes  isVarying only: sorted seconds-into-period list, one
    //               entry per occurrence (same anchor as offset)
    //--------------------------------------------------------------------------------
    // One occurrence within a CyclicGroup's ring.
    //
    // chatCode/shown/repeat/customColor/isVarying/varyingTimes are appended in this
    // order for the same positional-aggregate-init reason as WorldEvent's tail fields
    // (see events_basic.cpp/events_cyclic.cpp) - each field's position is how many
    // trailing values a compiled-in row must supply, so later/rarer fields go last.
    //--------------------------------------------------------------------------------
    struct Slot
    {
        std::string name;
        int         offset;
        int         duration;
        ColorTier   tier = ColorTier::Primary;

        std::string chatCode;
        bool        shown = true;
        int         repeat = 1;

        std::optional<ImU32> customColor;

        bool        isVarying = false;
        std::vector<int> varyingTimes;
    };

    std::vector<Slot> slots;

    std::optional<ImU32> idleColor;
    bool shown = true;
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

//_ Populated in events_cyclic.cpp, used by cyclicrender.cpp.
extern std::vector<CyclicGroup> g_CyclicGroups;

//********************************************************************************
// SlotOverride
//--------------------------------------------------------------------------------
// groupName/slotName   must match CyclicGroup::name / Slot::name exactly
// offset/duration      one-time-pushed onto the matching Slot when set
//--------------------------------------------------------------------------------
// Same purpose and version gate as CategoryDefaultMember::offset/duration
// (events_categories.h), but at Slot granularity, since a Slot's own fields
// aren't reachable through the group-level category mechanism.
//
// Applied by ApplySlotOverrides (events_storage.cpp) right after MergeGroups,
// gated the same way as ApplyCategoryOffsetOverrides: runs once while the saved
// file predates EVENTS_DATA_VERSION, which must be bumped whenever an entry is
// added here.
//--------------------------------------------------------------------------------
struct SlotOverride
{
    std::string groupName;
    std::string slotName;
    std::optional<int> offset;
    std::optional<int> duration;
};

//_ Populated in events_cyclic.cpp; consumed by ApplySlotOverrides.
extern std::vector<SlotOverride> g_SlotOverrides;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MIN
//--------------------------------------------------------------------------------
// Minutes -> seconds, for readable offsets/durations/periods in
// events_basic.cpp/events_cyclic.cpp.
//--------------------------------------------------------------------------------
constexpr int MIN(int minutes) { return minutes * 60; }

//_ Precomputed minute->second constants for the table below (m85/m110 unused).
constexpr int   m5=MIN(  5),  m10=MIN( 10),  m15=MIN( 15),  m20=MIN( 20),
               m25=MIN( 25),  m30=MIN( 30),  m35=MIN( 35),  m40=MIN( 40),
               m45=MIN( 45),  m50=MIN( 50),  m55=MIN( 55),  m60=MIN( 60),
               m65=MIN( 65),  m70=MIN( 70),  m75=MIN( 75),  m80=MIN( 80),
               m90=MIN( 90),  m95=MIN( 95), m100=MIN(100),
              m105=MIN(105), m115=MIN(115), m120=MIN(120);