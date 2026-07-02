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

// Zoom-based scaling. Markers stay at their base size (the settings
// above) from fully-zoomed-out up until BasicEventZoomStartPct of the
// way to fully-zoomed-in, then grow linearly, reaching
// BasicEventZoomMaxMultiplier * base size at 100% zoom (fully zoomed
// in). Compass.Scale itself is continent-units-per-pixel and has no
// fixed 0–100 range exposed by Mumble, so "percent zoom" is derived at
// render time from the current map's own min/max Compass.Scale — see
// GetZoomPercent() in maprender.cpp for that mapping.
SETTING(BasicEvents, BasicEventZoomScalingEnabled, bool,  true)
SETTING(BasicEvents, BasicEventZoomStartPct,       float, 50.0f) // % zoom at which growth begins
SETTING(BasicEvents, BasicEventZoomMaxMultiplier,  float, 2.0f)  // size multiplier at 100% zoom

// Calibration data for the self-learning zoom range described above —
// persisted so the very first frame after restarting the game/addon
// already has a usable range instead of starting back at "no variation
// observed yet" (which would mean a flat 0% / no growth) until the user
// zooms fully in and out again. -1 means "not yet calibrated".
SETTING(BasicEvents, BasicEventZoomScaleMinObserved, float, -1.0f)
SETTING(BasicEvents, BasicEventZoomScaleMaxObserved, float, -1.0f)

// Time-window filter — only show upcoming Basic Events that start within
// the next N minutes; currently-active events always show regardless.
// Stored in minutes, in 10-minute steps, up to 360 (6h). 0 means "no
// filter, show everything" — kept as a separate enabled flag rather than
// overloading 0-minutes-means-off, since a 0-minute *window* is also a
// theoretically valid (if useless) setting value to slide to.
//
// Deliberately NOT applied to cyclic groups — see RenderCyclicGroups in
// cyclicrender.cpp: a cyclic group's ring already shows its own rolling
// future/past window (CyclicMaxFutureDeg/CyclicMaxPastDeg) for every slot
// within the SAME ring, so a single group-level "hide the whole ring"
// time filter would conflict with that per-slot windowing rather than
// compose with it.
SETTING(BasicEvents, BasicEventTimeFilterEnabled,    bool, false)
SETTING(BasicEvents, BasicEventTimeFilterMinutes,    int,  60)

// ---------------------------------------------------------------------------
// [Subscriptions]
// ---------------------------------------------------------------------------
// Open/closed state of the standalone watchlist window (subscriptions.h /
// subscriptions_window.h). WHICH events are subscribed lives in
// events.json instead (see subscriptions.cpp) — same split as everything
// else in this file: settings.ini holds UI/display preferences, events.json
// holds the actual event/grouping/membership data.
SETTING(Subscriptions, ShowSubscriptionsWindow, bool, false)

// Open/closed state of the alternate "distribution bar" watchlist view
// (subscriptions_bar.h/.cpp) — same subscription data as
// ShowSubscriptionsWindow above, drawn as colored segments along a fixed
// 2h timeline strip instead of a text list. Independent toggle: either,
// both, or neither view can be open at once.
SETTING(Subscriptions, ShowSubscriptionsBar, bool, false)

// When true, active rows are simply left out of the watchlist window's
// list entirely (not just dimmed/recolored) — a "only show me what's NOT
// already happening" mode. Only affects the watchlist window; Basic Event
// markers on the map keep showing active events regardless (that's a
// separate, existing on/off-map concern with its own established colors,
// not something this toggle should also reach into).
SETTING(Subscriptions, SubscriptionsHideActive, bool, false)

// Text colors for the watchlist window's two highlighted states: an
// active row, and a row starting within the next 15 minutes ("soon" —
// same 900s threshold already used for BasicEventColorSoon on the map,
// reused here rather than introducing a second configurable window,
// per the call made this session). Rows that are neither just use the
// window's normal default text color, so there's no separate "waiting"
// setting the way the map markers have one — the list only needs to draw
// attention to the two states that are actually time-sensitive.
//
// Stored as packed RRGGBBAA like the map's BasicEventColor* settings, but
// note the LOW BYTE (alpha) is unused/ignored here: this feeds straight
// into ImGui::TextColored, which is plain text with no separate opacity
// control worth exposing, so the options-panel picker for these is a
// ColorEdit3 (RGB only, see addon_options.cpp) rather than ColorEdit4 —
// unlike BasicEventColor*, which DOES use every channel including alpha.
SETTING(Subscriptions, SubscriptionsActiveColor, unsigned int, 0x66FF66FFu) // light green
SETTING(Subscriptions, SubscriptionsSoonColor,   unsigned int, 0xFF8C00FFu) // orange, matches BasicEventColorSoon's RGB

SETTING(System, delayMilliseconds, int, 50)
