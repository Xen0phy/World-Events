#pragma once

// ---------------------------------------------------------------------------
// Subscriptions distribution bar
// ---------------------------------------------------------------------------
// A second, alternate view of the same watchlist data already shown by
// RenderSubscriptionsWindow (subscriptions_window.h/.cpp) — instead of a
// scrollable text list, this draws every subscribed Basic Event / Cyclic
// slot as a colored segment laid out along a fixed 2-hour timeline bar,
// so upcoming/active windows read at a glance as a strip of blocks rather
// than a stack of "in Xm Ys" rows. Visually modeled on the reference
// distribution-line.html mock (segments = colored blocks along a line,
// thin background-colored notches between adjacent segments).
//
// Each segment uses the SAME color the subscribed event/slot already has
// elsewhere in the addon: a Cyclic slot uses CyclicGroup::SlotColor(), a
// Basic Event uses a stable per-name color (Basic Events have no color of
// their own outside the map's global Active/Soon/Waiting scheme — see the
// long comment on BasicEventColorFor in subscriptions_bar.cpp for why a
// deterministic name hash is used instead of reusing that scheme here).
//
// Time range is fixed at 2 hours (now .. now+2h) — NOT user-configurable,
// per the call made this session; this deliberately keeps the bar reading
// as a fixed, comparable "next two hours" strip rather than a zoomable
// timeline. A "now" marker is drawn at the left edge; segments that are
// currently active are clipped to start at the marker (they already
// started before the visible window) and segments that don't start
// within the next 2 hours are simply not drawn.
//
// Visibility is a persisted setting (ShowSubscriptionsBar in
// settings_table.h), toggled from a button/menu entry in the options
// panel — see addon_options.cpp — same pattern as ShowSubscriptionsWindow.
// ---------------------------------------------------------------------------

// Draws the distribution bar window if ShowSubscriptionsBar is true. No-op
// (cheap early-out before any ImGui calls) when false. Call this once per
// frame from AddonRender, alongside RenderSubscriptionsWindow.
void RenderSubscriptionsBar();