#pragma once

// ---------------------------------------------------------------------------
// Subscriptions watchlist window
// ---------------------------------------------------------------------------
// A small, standalone, open/closeable ImGui window listing every
// subscribed Basic Event and Cyclic Event slot (see subscriptions.h), each
// with a live "Active" / "in Xm Ys" countdown — the same status text
// already shown in the map tooltips, just collected in one place so the
// user doesn't have to go hover markers on the map to check them.
//
// Visibility is a persisted setting (ShowSubscriptionsWindow in
// settings_table.h), toggled from a button/menu entry in the options
// panel — see addon_options.cpp.
// ---------------------------------------------------------------------------

// Draws the watchlist window if ShowSubscriptionsWindow is true. No-op
// (and cheap — an early-out before any ImGui calls) when false. Call this
// once per frame from AddonRender, same as RenderMapEvents/
// RenderCyclicGroups.
void RenderSubscriptionsWindow();
