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
