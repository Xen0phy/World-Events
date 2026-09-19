//################################################################################
// options_window.h
//--------------------------------------------------------------------------------
// ShowOptionsWindow          visibility flag; transient, never saved
// kOptionsWindowId           untranslated window ID suffix
// kOptionsTabCount           number of OptionsTab entries
// OptionsTab                 the four rail tabs
// OptionsDeepLink            one Basic/Cyclic/Live row a caller wants shown
// OpenOptionsWindow          open on the remembered tab, a given tab, or a row
// RenderOptionsWindow        draws the window; no-op unless open
// OptionsHighlight_Set       start the timed flash on one row id
// OptionsHighlight_IsActive
//                            true while that row's flash is running
//--------------------------------------------------------------------------------
// The single floating settings window: a left icon rail (General, Events, Live,
// with Help pinned to the bottom) beside a content pane that hosts exactly one
// section file per tab - options_general/events/live/help.cpp. This file owns
// Begin()/End(), the rail, the remembered tab, and deep-link delivery; the
// section files own everything drawn inside the content pane.
//
// The remembered tab is the OptionsWindowTab setting (settings_table.h).
// ShowOptionsWindow is not a setting: the window opens on request (the Nexus
// options button, or a right-click entry point calling OpenOptionsWindow) and its
// visibility does not survive a restart.
//
// Rendered from its own RT_Render callback (addon.cpp), outside AddonRender's
// IsGameplay gate, so it also opens at character select. Not gated by the
// competitive-mode switches: those govern passive overlays, not a window the
// player opened.
//--------------------------------------------------------------------------------

#pragma once

#include "subscriptions.h" //. SubscriptionKind, CyclicSubscriptionKey

#include <string>

//_ Transient only; the remembered tab is the persisted part (OptionsWindowTab).
extern bool ShowOptionsWindow;

//_ "###" keeps the ImGui ID stable across languages; shared by Begin() and the close-on-escape sync.
inline constexpr const char* kOptionsWindowId = "###WorldEventsOptions";

//_ Bounds the persisted OptionsWindowTab value.
inline constexpr int kOptionsTabCount = 4;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OptionsTab
//--------------------------------------------------------------------------------
// Rail order, top to bottom, Help pinned last. Stored in OptionsWindowTab as the
// int value, so an existing entry must never be renumbered.
//--------------------------------------------------------------------------------
enum class OptionsTab
{
    General = 0,
    Events  = 1,
    Live    = 2,
    Help    = 3,
};

//********************************************************************************
// OptionsDeepLink
//--------------------------------------------------------------------------------
// kind          Basic, Cyclic or Live (subscriptions.h)
// basicId       WorldEvent::id, meaningful when kind is Basic
// cyclicKey     group and slot ids, meaningful when kind is Cyclic
// liveEventId   LiveEvent::eventId, meaningful when kind is Live
//--------------------------------------------------------------------------------
// Identity of the one row a caller wants shown. Handed to DrawOptionsEvents /
// DrawOptionsLive by pointer for exactly one frame and nullptr on every other, so
// a stale target can never keep re-forcing a row open after the player collapses
// it.
//--------------------------------------------------------------------------------
struct OptionsDeepLink
{
    SubscriptionKind kind = SubscriptionKind::Basic;
    std::string basicId;
    CyclicSubscriptionKey cyclicKey;
    std::string liveEventId;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenOptionsWindow
//--------------------------------------------------------------------------------
// No argument: opens on the remembered tab. OptionsTab argument: opens on that
// tab.
//
// Row form: opens on the Live tab for kind Live, otherwise the Events tab, and
// hands the row identity to that section for exactly one frame (see
// OptionsDeepLink). liveEventId defaults to empty and only matters for kind Live.
//--------------------------------------------------------------------------------
void OpenOptionsWindow();
void OpenOptionsWindow(OptionsTab tab);
void OpenOptionsWindow(SubscriptionKind kind, const std::string& basicId,
    const CyclicSubscriptionKey& cyclicKey, const std::string& liveEventId = std::string());

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderOptionsWindow
//--------------------------------------------------------------------------------
// Call once per frame from its own RT_Render registration. No-op while
// ShowOptionsWindow is false. A pending deep link is consumed once Begin()
// confirms the window drew, and force-uncollapses the window for that frame so
// the target is visible.
//--------------------------------------------------------------------------------
void RenderOptionsWindow();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OptionsHighlight_Set / OptionsHighlight_IsActive
//--------------------------------------------------------------------------------
// Shared row-flash timer. A section calls OptionsHighlight_Set on the frame it
// consumes a deep link; each of its rows then asks OptionsHighlight_IsActive with
// its own id and tints itself while that is true (about 1.5 seconds). One flash
// at a time: a new Set replaces the previous one.
//--------------------------------------------------------------------------------
void OptionsHighlight_Set(const std::string& id);
bool OptionsHighlight_IsActive(const std::string& id);
