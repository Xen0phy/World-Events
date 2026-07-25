//################################################################################
// settings_table.h
//--------------------------------------------------------------------------------
// SETTING(Section, Key, Type, Default)      one scalar setting
// SETTING_ARRAY(Section, Key, N, Default)   one fixed-size float[N] setting
// SETTING_SECRET(Section, Key, Default)     std::string setting, encrypted on disk
// ARR(...)                                  brace-init helper for SETTING_ARRAY defaults
//--------------------------------------------------------------------------------
// Single source of truth for every persisted setting in World Events.
// Include with SETTING/SETTING_ARRAY/SETTING_SECRET defined to generate
// globals, INI I/O, or anything else that needs to touch every setting,
// then #undef them after the include.
//
// Section maps to an INI [Section] heading for human readability only -
// keys are matched by name alone on load, not scoped by section. Key is
// both the C++ variable name and the INI key name.
//
// DELIBERATELY NO include guard: this header is re-included once per
// macro-definition site, and settings.cpp includes it TWICE in the same
// translation unit (once via settings.h for the extern declarations, once
// directly for the storage definitions) - a guard would silently turn the
// second inclusion into a no-op, so the actual global storage would never
// get defined.
//--------------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SETTING
//--------------------------------------------------------------------------------
#ifndef SETTING
#define SETTING(S, Key, Type, Default)
#endif

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SETTING_SECRET
//--------------------------------------------------------------------------------
// Like SETTING, for settings whose on-disk form differs from their
// in-memory one - currently just Gw2ApiKey (encrypted in settings.ini, a
// plain std::string in memory). Defaults to forwarding to SETTING, so
// include sites that don't care (e.g. settings.cpp's storage-declaration
// site) get a normal std::string field; SaveSettings/LoadSettings define
// their own SETTING_SECRET to encrypt/decrypt at the point they'd
// otherwise write/parse the raw value. Kept as a real separate macro
// rather than a runtime branch inside SETTING so a non-string SETTING
// never has to type-check the encrypt/decrypt branch at all.
//--------------------------------------------------------------------------------
#ifndef SETTING_SECRET
#define SETTING_SECRET(S, Key, Default) SETTING(S, Key, std::string, Default)
#endif

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SETTING_ARRAY / ARR
//--------------------------------------------------------------------------------
// SETTING_ARRAY is SETTING for a fixed-size float[N] instead of one scalar
// - currently only colors (N=4: R,G,B,A in [0,1], the layout
// ImGui::ColorEdit3/4 read/write directly). Generic on N rather than
// hardcoding "color" in case a future setting needs some other float
// tuple.
//
// ARR(...) is just `{ ... }` under a name that reads better at the call
// site than a bare brace-init (ARR(0.2f, ...) reads as "this is the
// array", where the brace alone looks like a stray one).
//--------------------------------------------------------------------------------
#ifndef SETTING_ARRAY
#define SETTING_ARRAY(S, Key, N, Default)
#endif
#ifndef ARR
#define ARR(...) { __VA_ARGS__ }
#endif

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// [Cyclic]
//--------------------------------------------------------------------------------
SETTING(Cyclic, ShowCyclicOverlay,   bool,  true)
SETTING(Cyclic, CyclicRadius,        float, 20.0f)
SETTING(Cyclic, CyclicThickness,     float, 20.0f)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CyclicMaxFutureDeg / CyclicMaxPastDeg
//--------------------------------------------------------------------------------
// Stored as DEGREES, not seconds - period-independent, so 270 deg means
// "75% of however long this group's own cycle is", whether that's Dry
// Top's 3600s or everything else's 7200s. A flat seconds value can't hold
// that correctly across groups with different periods; see
// cyclicrender.cpp for the per-group deg->sec conversion.
//--------------------------------------------------------------------------------
SETTING(Cyclic, CyclicMaxFutureDeg,  float, 270.0f)
SETTING(Cyclic, CyclicMaxPastDeg,    float,  90.0f)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// [BasicEvents]
//--------------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventColorActive / BasicEventColorSoon / BasicEventColorWaiting
//--------------------------------------------------------------------------------
// One shared RGBA color per status, applied to every Basic Event (not
// per-event), used for both plain-dot and icon-tint rendering in
// maprender.cpp. ImGui::ColorEdit4 reads/writes these arrays directly;
// drawing converts via plain ImGui::ColorConvertFloat4ToU32 (color_
// utils.h) - no custom packing. No separate alpha setting: alpha lives in
// the 4th float, so whatever the user picks IS the actual opacity used.
//--------------------------------------------------------------------------------
SETTING_ARRAY(BasicEvents, BasicEventColorActive,  4, ARR(1.000f, 0.196f, 0.196f, 0.706f)) //. red, was 0xFF3232B4u
SETTING_ARRAY(BasicEvents, BasicEventColorSoon,    4, ARR(1.000f, 0.549f, 0.000f, 0.706f)) //. orange, was 0xFF8C00B4u
SETTING_ARRAY(BasicEvents, BasicEventColorWaiting, 4, ARR(0.627f, 0.627f, 0.627f, 0.706f)) //. gray, was 0xA0A0A0B4u

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventDotRadius / BasicEventIconSize
//--------------------------------------------------------------------------------
// Size in pixels, independent of each other. BasicEventIconSize is the
// icon's HALF-width (matching maprender.cpp); height still derives from
// the icon texture's own aspect ratio, not a separate setting, so
// user-supplied icons never render stretched.
//--------------------------------------------------------------------------------
SETTING(BasicEvents, BasicEventDotRadius, float, 8.0f)
SETTING(BasicEvents, BasicEventIconSize,  float, 12.0f)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventZoomScalingEnabled / BasicEventZoomStartPct / BasicEventZoomMaxMultiplier
//--------------------------------------------------------------------------------
// Zoom-based marker scaling. Markers stay at base size from fully-zoomed-
// out until StartPct of the way to fully-zoomed-in, then grow linearly to
// MaxMultiplier * base size at 100% zoom. Compass.Scale has no fixed
// 0-100 range from Mumble, so "percent zoom" is derived at render time
// from the current map's own min/max Compass.Scale - see
// GetZoomPercent() in maprender.cpp.
//--------------------------------------------------------------------------------
SETTING(BasicEvents, BasicEventZoomScalingEnabled, bool,  true)
SETTING(BasicEvents, BasicEventZoomStartPct,       float, 0.0f)
SETTING(BasicEvents, BasicEventZoomMaxMultiplier,  float, 3.0f)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventZoomScaleMinObserved / BasicEventZoomScaleMaxObserved
//--------------------------------------------------------------------------------
// Calibration data for the zoom range above, persisted so the first frame
// after restarting already has a usable range instead of a flat 0%/no
// growth until the user zooms fully in and out again. -1 means "not yet
// calibrated".
//--------------------------------------------------------------------------------
SETTING(BasicEvents, BasicEventZoomScaleMinObserved, float, -1.0f)
SETTING(BasicEvents, BasicEventZoomScaleMaxObserved, float, -1.0f)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BasicEventTimeFilterEnabled / BasicEventTimeFilterMinutes
//--------------------------------------------------------------------------------
// Only show upcoming Basic Events starting within the next N minutes;
// currently-active events always show regardless. Minutes, in 10-minute
// steps up to 360 (6h); a separate enabled flag gates the filter rather
// than using 0 as an "off" sentinel, since 0 is also a theoretically
// valid window value. Deliberately NOT applied to cyclic groups - see
// RenderCyclicGroups in cyclicrender.cpp: a cyclic group's ring already
// shows its own rolling window per slot, and a group-level "hide the
// whole ring" filter would conflict with that rather than compose.
//--------------------------------------------------------------------------------
SETTING(BasicEvents, BasicEventTimeFilterEnabled,    bool, false)
SETTING(BasicEvents, BasicEventTimeFilterMinutes,    int,  60)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// [Subscriptions]
//--------------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShowSubscriptionsWindow
//--------------------------------------------------------------------------------
// Open/closed state of the standalone watchlist window (subscriptions.h /
// subscriptions_window.h). WHICH events are subscribed lives in
// events.json instead (subscriptions.cpp) - settings.ini holds UI/display
// preferences, events.json holds the actual event/grouping/membership
// data.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, ShowSubscriptionsWindow, bool, false)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ShowSubscriptionsBar
//--------------------------------------------------------------------------------
// Open/closed state of the alternate "distribution bar" watchlist view
// (subscriptions_bar.h/.cpp) - same subscription data as
// ShowSubscriptionsWindow, drawn as colored segments on a fixed 2h
// timeline strip instead of a text list. Independent toggle: either, both,
// or neither view can be open at once.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, ShowSubscriptionsBar, bool, false)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsBarMinimalMode / SubscriptionsBarBottomAnchored
//--------------------------------------------------------------------------------
// Two independent display-mode toggles for the distribution bar
// (subscriptions_bar.cpp). MinimalMode strips the bar to bare colored
// blocks - every subscribed event/slot gets its own dot on the baseline
// (CollectAllEventDots) instead of only lane>0 overlaps getting a dot
// underneath the lane-0 line (CollectOverlapDots). BottomAnchored pins the
// bar to the bottom screen edge and flips every drop/pop-out direction to
// grow upward, instead of the default top-pinned, drop-downward layout.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, SubscriptionsBarMinimalMode,     bool, false)
SETTING(Subscriptions, SubscriptionsBarBottomAnchored,  bool, false)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsBarDotColor
//--------------------------------------------------------------------------------
// Default dot color for the subscription bar (alpha suppressed in the
// options UI for visibility); plain white by default, was 0xFEFFFEFFu.
SETTING_ARRAY(Subscriptions, SubscriptionsBarDotColor, 4, ARR(0.996f, 1.000f, 0.996f, 1.000f))

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsBarHoverDelayMs
//--------------------------------------------------------------------------------
// How long the mouse must sit still over a distribution-bar segment/dot
// (subscriptions_bar.cpp) before its curve-drop hover animation starts -
// avoids every segment popping in and out as the mouse merely passes over
// the top edge en route elsewhere. 0 disables the delay (drop starts
// instantly on touch).
//--------------------------------------------------------------------------------
SETTING(Subscriptions, SubscriptionsBarHoverDelayMs, int, 500)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsBarUnsafeLeftPx / SubscriptionsBarUnsafeRightPx / SubscriptionsBarUnsafeHeightPx
//--------------------------------------------------------------------------------
// GW2's own UI (party/buffs top-left, minimap/compass top-right) leaves
// only ~8px of free space right under the top edge there, not enough for
// a dropped block's label; the wide middle strip has plenty, so the drop
// only needs to move for segments whose x-range falls inside these edge
// margins (see subscriptions_bar.cpp's SegmentOverlapsUnsafeZone).
//
// Left/Right are each measured inward from their own screen edge and are
// independent, since GW2's left/right top UI blocks differ in width (0
// disables a zone individually, dropping straight down there). HeightPx
// is how tall that corner UI is, in px, that the drop needs to clear -
// one shared value for both corners, not per-side.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, SubscriptionsBarUnsafeLeftPx,   int, 0)
SETTING(Subscriptions, SubscriptionsBarUnsafeRightPx,  int, 0)
SETTING(Subscriptions, SubscriptionsBarUnsafeHeightPx, int, 90)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsBarMaxDropPx
//--------------------------------------------------------------------------------
// How far a fully-hovered segment drops from the baseline, and, once
// pill-detach kicks in for unsafe-zone segments, the pill's fixed height
// too - deliberately unified into one value (pill height must equal a
// normal pop-out's height exactly). Sized by default to fit two centered
// lines of label text.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, SubscriptionsBarMaxDropPx, int, 50)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsHideActive / SubscriptionsBarHideActive
//--------------------------------------------------------------------------------
// "Only show me what's NOT already happening": active rows/segments are
// left out of the watchlist window's list / distribution bar's
// CollectVisibleSegments output entirely (not just dimmed), so lanes,
// dots, stacking and drawing never see them. Two independent settings,
// not one shared, since a user may want one view filtered without the
// other. Basic Event markers on the map are unaffected either way - a
// separate, existing on/off-map concern.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, SubscriptionsHideActive, bool, false)
SETTING(Subscriptions, SubscriptionsBarHideActive, bool, false)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionsActiveColor / SubscriptionsSoonColor
//--------------------------------------------------------------------------------
// Text colors for the watchlist window's two highlighted states: an
// active row, and one starting within 15 minutes ("soon" - same 900s
// threshold as BasicEventColorSoon, reused rather than adding a second
// configurable window). Rows that are neither use the window's normal
// text color, so there's no separate "waiting" setting the way map
// markers have.
//
// Stored as RGBA like the map's BasicEventColor* settings, but the 4th
// component (alpha) is unused: this feeds ImGui::TextColored directly, so
// the options-panel picker is a ColorEdit3 (RGB only). The unused slot is
// still stored so ToImVec4/ToImVec4Opaque (color_utils.h) can stay one
// shared helper for every color setting.
//--------------------------------------------------------------------------------
SETTING_ARRAY(Subscriptions, SubscriptionsActiveColor, 4, ARR(0.400f, 1.000f, 0.400f, 1.000f)) //. light green, was 0x66FF66FFu
SETTING_ARRAY(Subscriptions, SubscriptionsSoonColor,   4, ARR(1.000f, 0.549f, 0.000f, 1.000f)) //. matches BasicEventColorSoon's RGB

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WeeklyAutoTrackEnabled
//--------------------------------------------------------------------------------
// Master switch for the "auto-tracked weekly Wizard's Vault target"
// overlay shared by all three subscription views. When true (default),
// any Basic Event/Cyclic slot that's an active-and-incomplete target of
// THIS WEEK's Vault rotation (weekly_vault.h) is surfaced in all three
// views even if the user never manually subscribed to it. When false,
// each view falls back to showing only g_SubscribedBasicEvents/
// g_SubscribedCyclicSlots, as if weekly_vault.h didn't exist.
//
// Does NOT touch the small red "counts toward this week's Vault
// objective" marker drawn on a row that IS manually subscribed - that's
// just an informational tag on something the user already chose to
// track, so it keeps showing regardless of this setting.
//--------------------------------------------------------------------------------
SETTING(Subscriptions, WeeklyAutoTrackEnabled, bool, true)

//_ Default color for the weekly-target marker on the bar/window/toast
// border (alpha suppressed for visibility); red by default, was 0xFF2828FFu.
SETTING_ARRAY(Subscriptions, WeeklyAutoTrackColor, 4, ARR(1.000f, 0.157f, 0.157f, 1.000f))

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Gw2ApiKey
//--------------------------------------------------------------------------------
// GW2 API key (needs at minimum the "progression" permission), used ONLY
// to call GET /v2/account/worldbosses (gw2_api.h/.cpp). Drives
// automatically hiding a subscribed Core Boss from the Subscriptions
// window/bar once the account has already killed it since the last UTC
// daily reset. Empty = feature off, nothing hidden, no requests made (see
// PollGw2Api's early-out).
//
// This global always holds the PLAINTEXT key at runtime (what
// gw2_api.cpp sends over HTTPS). On disk it's AES-256-GCM-encrypted, with
// the master key in a separate "apikey.key" file next to settings.ini -
// see apikey_crypto.h/.cpp for the scheme, and settings.cpp for where
// this key is special-cased out of the generic write/parse path every
// other setting uses. Pre-encryption settings.ini files (plaintext key)
// still load fine and get re-saved encrypted automatically.
//--------------------------------------------------------------------------------
SETTING_SECRET(Subscriptions, Gw2ApiKey, std::string())

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// [System]
//--------------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// delayMilliseconds
//--------------------------------------------------------------------------------
// Delay, in ms, PasteToChat (subscriptions.cpp) waits between each step
// of its simulated Enter -> Ctrl+V -> Enter sequence when a watchlist
// row/segment/popup is clicked. Shared by all three subscription views,
// since they all paste through the same PasteToChat helper - not
// "Subscriptions"-scoped itself, since chat-paste timing is a general
// input-simulation concern rather than a display preference, though it
// currently has no other caller.
//--------------------------------------------------------------------------------
SETTING(System, delayMilliseconds, int, 20)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ChatChannelPrefix
//--------------------------------------------------------------------------------
// Slash-command prefix prepended to every PasteToChat message (see
// BuildChatPasteMessage in subscriptions.cpp) so a click always lands in
// a specific chat channel regardless of which tab has keyboard focus.
// Empty (default) pastes exactly as before. One value covers all three
// subscription views (same reasoning as delayMilliseconds above), stored
// as the literal command text (e.g. "/p ") rather than an enum index, so
// the options-panel Combo (addon_options.cpp) is the only place that
// needs the full label<->command mapping.
//--------------------------------------------------------------------------------
SETTING(System, ChatChannelPrefix, std::string, std::string())

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// [Notifications]
//--------------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// NotificationsEnabled
//--------------------------------------------------------------------------------
// A fourth view of the same subscription data as the window/bar (see
// subscriptions_notification.h/.cpp): small "toast" popups in the
// lower-right corner instead of a persistent list/strip. Master switch -
// when false, RenderSubscriptionsNotifications() is a complete no-op,
// same early-out pattern as ShowSubscriptionsWindow/ShowSubscriptionsBar.
//--------------------------------------------------------------------------------
SETTING(Notifications, NotificationsEnabled, bool, false)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// NotificationLeadMinutes
//--------------------------------------------------------------------------------
// Minutes before a subscribed occurrence starts to fire the "starting
// soon" popup. Fires once per occurrence, re-armed the moment that
// occurrence's active window closes (see s_notifyStates/
// NotifyState::leadFired in subscriptions_notification.cpp). 0 disables
// this popup; the on-start popup below is independent and still fires.
//--------------------------------------------------------------------------------
SETTING(Notifications, NotificationLeadMinutes, int, 5)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// NotificationOnStart
//--------------------------------------------------------------------------------
// Second, independent popup: fires the instant a subscribed occurrence
// actually goes active, regardless of whether the lead-time popup above
// already fired. Its own on/off, since a user may want only the advance
// warning, only the "it's live" ping, or both.
//--------------------------------------------------------------------------------
SETTING(Notifications, NotificationOnStart, bool, false)

//_ How long a popup stays fully visible before fading, in seconds -
// purely cosmetic, no bearing on whether/when a popup fires.
SETTING(Notifications, NotificationDisplaySeconds, int, 10)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// NotificationSoundFile
//--------------------------------------------------------------------------------
// Filename (no path) of a single user-supplied .wav under "<addon dir>/
// sounds", picked via the Combo next to the "Test" button in the options
// panel (see notify_sound.h for the scan/playback plumbing). Empty
// (default) means no sound file selected. Played by
// subscriptions_notification.cpp alongside a fired popup for any
// event/slot whose notify level has sound enabled (level 3 - see
// subscriptions.h); the "Test" button plays it unconditionally.
//--------------------------------------------------------------------------------
SETTING(Notifications, NotificationSoundFile, std::string, std::string())