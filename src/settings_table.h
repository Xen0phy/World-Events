// settings_table.h
// Single source of truth for every persisted setting in World Events.
//
// Include this file with SETTING / SETTING_FLOAT defined to generate
// globals, INI I/O, or anything else that needs to touch every setting.
// After each include site, #undef the macros you defined.
//
// Macro signature:
//   SETTING(Section, Key, Type, Default)
//
// Section maps to an INI [Section] heading (for human readability only —
// keys are matched by name alone on load, not scoped by section).
// Key     is both the C++ variable name and the INI key name.
//
// DELIBERATELY NO #pragma once / include guard here: this header is meant
// to be re-included multiple times — once per macro definition site — and
// in settings.cpp specifically it's included TWICE in the same translation
// unit (once transitively via settings.h for the extern declarations, once
// directly for the storage definitions). An include guard would silently
// turn the second inclusion into a no-op, meaning the actual global storage
// would never be defined — a real, easy-to-miss failure mode for this
// pattern if a guard is added without thinking through every include site.

#ifndef SETTING
#define SETTING(S, Key, Type, Default)
#endif

// ---------------------------------------------------------------------------
// [Cyclic]
// ---------------------------------------------------------------------------
SETTING(Cyclic, ShowCyclicOverlay,   bool,  true)
SETTING(Cyclic, CyclicRadius,        float, 20.0f)
SETTING(Cyclic, CyclicThickness,     float, 40.0f)

// Stored as DEGREES, not seconds. Degrees are period-independent — 270°
// means "75% of however long this particular group's cycle is", whether
// that's Dry Top's 3600s or everything else's 7200s. Seconds are NOT
// period-independent (270° of a 3600s cycle is a different number of
// seconds than 270° of a 7200s cycle), and a single flat settings value
// can't hold "seconds" correctly for groups with different periods anyway —
// see cyclicrender.cpp for the per-group conversion (SECS_PER_DEG is
// computed from each group's own `period`, not a hardcoded constant).
SETTING(Cyclic, CyclicMaxFutureDeg,  float, 270.0f)
SETTING(Cyclic, CyclicMaxPastDeg,    float,  90.0f)

// ---------------------------------------------------------------------------
// [BasicEvents]
// ---------------------------------------------------------------------------
// Status colors for the plain-dot AND icon-tint rendering in
// maprender.cpp — one shared color per status, applied to every Basic
// Event (not per-event; see DrawBulkIconPicker's "All icons" picker for
// the equivalent all-at-once pattern already used for icon choice).
// Stored as packed RRGGBBAA unsigned ints, matching CyclicGroup::colors'
// existing convention in cyclic.h, rather than introducing a new
// SETTING_ARRAY macro variant just for this.
//
// No separate alpha setting: previously each status color had a
// hardcoded 180/255 alpha baked in at the call site in maprender.cpp:
// IM_COL32(r, g, b, 180). These three settings REPLACE that entirely —
// whatever alpha the user picks via the color swatch (the 4th channel,
// part of the packed RRGGBBAA value below) IS the actual opacity used,
// with nothing layered on top of it.
SETTING(BasicEvents, BasicEventColorActive,  unsigned int, 0xFF3232B4u) // red,    matches the old IM_COL32(255,50,50,180)
SETTING(BasicEvents, BasicEventColorSoon,    unsigned int, 0xFF8C00B4u) // orange, matches the old IM_COL32(255,140,0,180)
SETTING(BasicEvents, BasicEventColorWaiting, unsigned int, 0xA0A0A0B4u) // gray,   matches the old IM_COL32(160,160,160,180)

// Size, in pixels. Independent of each other — changing one does NOT
// affect the other, even though the icon size used to be derived from
// the dot's radius (RADIUS * 1.5) before this. BasicEventIconSize is the
// icon's HALF-width (matching how maprender.cpp already computes it);
// height is still derived from the icon texture's own aspect ratio, not
// a separate setting, so user-supplied icons never render stretched.
SETTING(BasicEvents, BasicEventDotRadius, float, 8.0f)  // matches the old hardcoded RADIUS
SETTING(BasicEvents, BasicEventIconSize,  float, 12.0f) // matches the old hardcoded RADIUS(8.0f) * 1.5f
