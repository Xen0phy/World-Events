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

// Same idea as SETTING above, but for settings whose ON-DISK representation
// needs to differ from their in-memory one — currently just Gw2ApiKey (see
// below), which is encrypted in settings.ini but a plain std::string in
// memory. Defaults to forwarding straight to SETTING (so include sites that
// don't care about the distinction, like the storage-declaration site in
// settings.cpp, just get a normal std::string field); settings.cpp's
// SaveSettings/LoadSettings each define their own SETTING_SECRET instead,
// to encrypt/decrypt at the point they'd otherwise write/parse the raw
// value. Kept as a genuinely separate macro (not a runtime strcmp branch
// inside SETTING) so each call site only ever compiles the branch that
// actually matches std::string — a runtime branch would still require both
// branches to type-check for every non-string SETTING too.
#ifndef SETTING_SECRET
#define SETTING_SECRET(S, Key, Default) SETTING(S, Key, std::string, Default)
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
// existing convention in events.h, rather than introducing a new
// SETTING_ARRAY macro variant just for this.
//
// No separate alpha setting: alpha lives in the color itself (the 4th
// channel of the packed RRGGBBAA value below) — whatever alpha the user
// picks via the color swatch IS the actual opacity used, nothing layered
// on top of it.
SETTING(BasicEvents, BasicEventColorActive,  unsigned int, 0xFF3232B4u) // red
SETTING(BasicEvents, BasicEventColorSoon,    unsigned int, 0xFF8C00B4u) // orange
SETTING(BasicEvents, BasicEventColorWaiting, unsigned int, 0xA0A0A0B4u) // gray

// Size, in pixels. Independent of each other — changing one does NOT
// affect the other. BasicEventIconSize is the icon's HALF-width (matching
// how maprender.cpp already computes it); height is still derived from
// the icon texture's own aspect ratio, not a separate setting, so
// user-supplied icons never render stretched.
SETTING(BasicEvents, BasicEventDotRadius, float, 8.0f)
SETTING(BasicEvents, BasicEventIconSize,  float, 12.0f)

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
// Stored in minutes, in 10-minute steps, up to 360 (6h). A separate
// enabled flag gates the filter, rather than using 0 minutes as an
// "off" sentinel, since 0 is also a theoretically valid (if useless)
// window value to slide to.
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

// Two display-mode toggles for the distribution bar (subscriptions_bar.cpp),
// independent of each other:
//
// SubscriptionsBarMinimalMode strips the bar down to bare colored blocks —
// no per-slot lane/curve-drop layout — trading detail for a slimmer strip.
// When on, every subscribed event/slot gets its own dot sitting directly on
// the baseline (CollectAllEventDots) instead of only lane>0 overlaps
// getting a dot underneath the visible lane-0 line (CollectOverlapDots).
//
// SubscriptionsBarBottomAnchored pins the bar to the bottom screen edge and
// flips every drop/pop-out direction to grow upward instead of down,
// instead of the default top-pinned, drop-downward layout.
SETTING(Subscriptions, SubscriptionsBarMinimalMode,     bool, false)
SETTING(Subscriptions, SubscriptionsBarBottomAnchored,  bool, false)

// How long the mouse must sit still over a distribution-line segment or
// dot marker (subscriptions_bar.cpp) before its curve-drop hover
// animation starts — avoids every segment along the strip popping in and
// out as the mouse merely passes over the top edge of the screen on its
// way to/from somewhere else (e.g. the character select / login screen
// menus, or just moving the mouse up to click a Nexus icon). 0 disables
// the delay entirely (drop starts the instant the mouse touches a
// segment).
SETTING(Subscriptions, SubscriptionsBarHoverDelayMs, int, 500)

// The corners of the screen right under the top edge are where GW2's own
// UI actually lives (party/buffs top-left, minimap/compass top-right) —
// there's only ~8px of genuinely free space directly under the line
// there, nowhere near enough for a dropped block's label to be legible
// without covering something. The wide middle strip of the screen has
// much more free space, which is why the drop only needs to move for
// segments whose x-range falls inside these edge margins.
//
// SubscriptionsBarUnsafeLeftPx / SubscriptionsBarUnsafeRightPx are each
// measured inward from their respective screen edge (not from the
// center) and are independent since GW2's left and right top UI blocks
// aren't the same width (default 300 covers the default UI's compact
// party/buff area on the left; SubscriptionsBarUnsafeRightPx covers the
// minimap/compass on the right — see subscriptions_bar.cpp's
// SegmentOverlapsUnsafeZone). 0 disables the left or right zone
// individually, dropping straight down everywhere on that side.
SETTING(Subscriptions, SubscriptionsBarUnsafeLeftPx,  int, 300)
SETTING(Subscriptions, SubscriptionsBarUnsafeRightPx, int, 300)

// How far down a dropped block starts (instead of the line itself) when
// its segment falls inside either unsafe zone above — i.e. how tall the
// corner UI actually is, in px, that the drop needs to clear. Same value
// for both corners; if one side's UI is taller than the other in a
// user's particular layout, they can only push this number up for both,
// not per-side — that asymmetry wasn't worth a second setting for what's
// already a corner-case (no pun intended) escape hatch.
SETTING(Subscriptions, SubscriptionsBarUnsafeHeightPx, int, 90)

// How far a fully-hovered segment drops down from the baseline (attached
// pop-out block), and, once pill-detach kicks in for unsafe-zone segments,
// the pill's fixed height too — the two are deliberately unified into one
// value (pill height must equal a normal safe-zone pop-out's height
// exactly; there is no separate pill-height constant). Sized by default
// to comfortably fit two centered lines of label text; raise it if your
// font/DPI settings need more room, lower it for a more compact pop-out.
SETTING(Subscriptions, SubscriptionsBarMaxDropPx, int, 54)

// When true, active rows are simply left out of the watchlist window's
// list entirely (not just dimmed/recolored) — a "only show me what's NOT
// already happening" mode. Only affects the watchlist window; Basic Event
// markers on the map keep showing active events regardless (that's a
// separate, existing on/off-map concern with its own established colors,
// not something this toggle should also reach into).
SETTING(Subscriptions, SubscriptionsHideActive, bool, false)

// Same idea as SubscriptionsHideActive above, but for the distribution
// bar (subscriptions_bar.cpp) instead of the watchlist window — its own
// setting, not reused, since the two views are explicitly independent
// (see ShowSubscriptionsBar's comment) and a user may want one filtered
// without the other. A segment that's currently active is simply left
// out of CollectVisibleSegments' output entirely when this is true, same
// as SubscriptionsHideActive's window-list filtering — so lanes, dots,
// stacking and drawing never see it and nothing else about the bar's
// layout changes; an active segment just isn't there.
SETTING(Subscriptions, SubscriptionsBarHideActive, bool, false)

// Text colors for the watchlist window's two highlighted states: an
// active row, and a row starting within the next 15 minutes ("soon" —
// same 900s threshold already used for BasicEventColorSoon on the map,
// reused here rather than introducing a second configurable window).
// Rows that are neither just use the window's normal default text color,
// so there's no separate "waiting" setting the way the map markers have
// one — the list only needs to draw attention to the two states that are
// actually time-sensitive.
//
// Stored as packed RRGGBBAA like the map's BasicEventColor* settings, but
// note the LOW BYTE (alpha) is unused/ignored here: this feeds straight
// into ImGui::TextColored, which is plain text with no separate opacity
// control worth exposing, so the options-panel picker for these is a
// ColorEdit3 (RGB only, see addon_options.cpp) rather than ColorEdit4 —
// unlike BasicEventColor*, which DOES use every channel including alpha.
SETTING(Subscriptions, SubscriptionsActiveColor, unsigned int, 0x66FF66FFu) // light green
SETTING(Subscriptions, SubscriptionsSoonColor,   unsigned int, 0xFF8C00FFu) // orange, matches BasicEventColorSoon's RGB

// Master switch for the "auto-tracked weekly Wizard's Vault target" overlay
// shared by all three subscription views (subscriptions_window.cpp,
// subscriptions_bar.cpp, subscriptions_notification.cpp): when true (the
// default), any Basic Event / Cyclic slot that is an active-and-incomplete
// target of THIS WEEK's Vault rotation (weekly_vault.h) is surfaced in all
// three views even if the user never manually subscribed to it themselves.
// When false, none of the three auto-add anything — each view falls back
// to showing only what's actually in g_SubscribedBasicEvents/
// g_SubscribedCyclicSlots, exactly as if weekly_vault.h didn't exist.
//
// Deliberately does NOT touch the small red "counts toward this week's
// Wizard's Vault objective" marker/border drawn on a row/segment/popup that
// IS manually subscribed — that's just an informational tag on something
// the user already chose to track, not the addon auto-adding anything on
// its own, so it keeps showing regardless of this setting.
SETTING(Subscriptions, WeeklyAutoTrackEnabled, bool, false)

// GW2 API key (needs at minimum the "progression" permission), used ONLY
// to call GET /v2/account/worldbosses — see gw2_api.h/.cpp. Drives
// automatically hiding a subscribed Core Boss from the Subscriptions
// window/bar once the account has already killed it since the last UTC
// daily reset. Empty = feature off; nothing is hidden, and no requests
// are made (see PollGw2Api's early-out).
//
// This global always holds the PLAINTEXT key at runtime (that's what
// gw2_api.cpp sends over HTTPS). On disk it's a different story:
// settings.ini stores it AES-256-GCM-encrypted, with the master key kept
// in a separate "apikey.key" file next to settings.ini — see
// apikey_crypto.h/.cpp for the scheme and its threat model, and
// settings.cpp for where Gw2ApiKey is special-cased out of the generic
// write/parse path every other setting here uses. Pre-encryption
// settings.ini files (plaintext key) still load fine and get re-saved
// encrypted automatically.
SETTING_SECRET(Subscriptions, Gw2ApiKey, std::string())

// ---------------------------------------------------------------------------
// [System]
// ---------------------------------------------------------------------------
// Delay, in milliseconds, PasteToChat (subscriptions.cpp) waits between each
// step of its simulated Enter -> Ctrl+V -> Enter keystroke sequence when a
// watchlist row, distribution-bar segment, or notification popup is clicked.
// Shared by all three subscription views (subscriptions_window.cpp,
// subscriptions_bar.cpp, subscriptions_notification.cpp), since they all
// paste through this same PasteToChat helper — not "Subscriptions"-scoped
// itself because chat-paste timing is a general input-simulation concern
// rather than a subscriptions-specific display preference, though it
// currently has no other caller.
SETTING(System, delayMilliseconds, int, 50)

// Slash-command prefix prepended to every PasteToChat message (see
// BuildChatPasteMessage in subscriptions.cpp) so a watchlist row/segment/
// toast click always lands in a specific chat channel, regardless of
// whichever channel tab currently has keyboard focus in-game. Empty (the
// default) pastes exactly as before — no prefix, whatever channel is
// already selected. One value covers all three subscription views (same
// "not Subscriptions-scoped" reasoning as delayMilliseconds just above),
// stored as the literal command text itself (e.g. "/p ") rather than an
// enum index, so the options-panel Combo (addon_options.cpp) is the only
// place that needs to know the full label<->command mapping.
SETTING(System, ChatChannelPrefix, std::string, std::string())

// ---------------------------------------------------------------------------
// [Notifications]
// ---------------------------------------------------------------------------
// A fourth view of the same subscription data as the window/bar above (see
// subscriptions_notification.h/.cpp): small "toast" popups in the
// lower-right corner instead of a persistent list/strip. Master on/off
// switch — when false, RenderSubscriptionsNotifications() is a complete
// no-op (no state tracked, no popups fired, nothing drawn), same early-out
// pattern as ShowSubscriptionsWindow/ShowSubscriptionsBar above.
SETTING(Notifications, NotificationsEnabled, bool, false)

// How many minutes before a subscribed Basic Event or Cyclic slot's next
// occurrence starts to fire the "starting soon" popup. Fires once per
// upcoming occurrence (re-armed the moment that occurrence's active window
// closes — see s_notifyStates/NotifyState::leadFired in
// subscriptions_notification.cpp). 0 disables this popup entirely; the
// on-start popup below is independent and still fires regardless.
SETTING(Notifications, NotificationLeadMinutes, int, 5)

// Second, independent popup: fired the instant a subscribed Basic Event or
// Cyclic slot actually goes active, regardless of whether the lead-time
// popup above already fired for it. Its own on/off, since a user may want
// only the advance warning, only the "it's live" ping, or both.
SETTING(Notifications, NotificationOnStart, bool, true)

// How long a popup stays fully visible before it starts fading out, in
// seconds. Purely cosmetic — has no bearing on whether/when a popup fires,
// only how long it lingers on screen once it has.
SETTING(Notifications, NotificationDisplaySeconds, int, 8)

// Filename (no path) of a single user-supplied .wav under
// "<addon dir>/sounds", picked via the Combo next to the "Test" button in
// the options panel (addon_options.cpp) — see notify_sound.h for the
// scan/playback plumbing. Empty (the default) means no sound file is
// selected. Played by subscriptions_notification.cpp alongside a fired
// "starting soon"/"now active" popup for any event/slot whose own notify
// level has sound enabled (level 3 — see subscriptions.h); the "Test"
// button plays it unconditionally regardless of any subscription's
// notify level.
SETTING(Notifications, NotificationSoundFile, std::string, std::string())