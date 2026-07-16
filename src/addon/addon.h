#pragma once

#include "Nexus.h"
#include "Mumble.h"
#include <string>
#include <chrono>

// Global API pointers — set in AddonLoad, valid until AddonUnload
extern AddonAPI_t*      APIDefs;
extern Mumble::Data*    MumbleLink;
extern NexusLinkData_t* NexusLink;

// Addon's own data directory (e.g. "<GW2>/addons/WorldEvents"), set once in
// AddonLoad via APIDefs->Paths_GetAddonDirectory. Used for settings.ini and
// any future JSON data files.
extern std::string g_AddonDir;

// ---------------------------------------------------------------------------
// Debug switch — build-time only, deliberately NOT a settings_table.h
// SETTING. Flip this constant and rebuild rather than exposing it as a
// real user-facing option; it isn't persisted anywhere. Gates the render-
// timing measurement below (see ScopedRenderTimer in this file) and the debug
// line addon_options.cpp shows underneath the release date/time.
// ---------------------------------------------------------------------------
inline constexpr bool ShowDebug = false;

// Rolling average, in milliseconds, of how long AddonRender's own body
// took to run — updated about once a second (see ScopedRenderTimer
// below), rather than showing a single frame's noisy raw time. Only
// ever written while ShowDebug is true; left at 0 otherwise.
extern float g_AvgRenderTimeMs;

// Same idea as g_AvgRenderTimeMs, but split further, per view, into the
// two phases each of RenderSubscriptionsBar/Window/Notifications actually
// has: "Data" is gathering/resolving what to show (now mostly
// RefreshSubscriptionsCache + a light per-view adaptation — see
// subscriptions_cache.h) and "Draw" is the actual ImGui calls that turn
// that into pixels. Split out specifically to answer "is the remaining
// per-frame cost in re-deriving data or in rendering it" — before this,
// g_AvgRenderTimeMs only gave one combined number for all three views
// together, which couldn't distinguish the two.
extern float g_AvgSubsBarDataMs,      g_AvgSubsBarDrawMs;
extern float g_AvgSubsWindowDataMs,   g_AvgSubsWindowDrawMs;
extern float g_AvgSubsNotifyDataMs,   g_AvgSubsNotifyDrawMs;

// Same idea as g_AvgRenderTimeMs, but for AddonOptions's own body instead
// of AddonRender's — the options panel rebuilds its whole group/slot tree,
// search filter, and color pickers every frame it's open, which is a
// separate (and often larger) cost from the actual map/subscriptions
// render. Kept as its own accumulator so the two never get lumped
// together — see ScopedRenderTimer in addon.cpp.
extern float g_AvgOptionsRenderTimeMs;

// ---------------------------------------------------------------------------
// ScopedRenderTimer<TargetAvg>
// ---------------------------------------------------------------------------
// RAII scope timer, templated on WHICH global accumulator it feeds. Every
// distinct template argument is a distinct type, so RenderTimer and
// OptionsRenderTimer below each get their own destructor and therefore
// their own function-local static accumulator state — instantiating both
// in the same process never mixes their totals, unlike a single shared
// non-template timer would if used at two call sites.
//
// if constexpr on ShowDebug means this compiles down to an empty
// constructor/destructor (not even a steady_clock::now() call) when
// ShowDebug is false, so leaving these in place has no runtime cost in a
// normal build.
// ---------------------------------------------------------------------------
template <float& TargetAvg>
struct ScopedRenderTimer
{
    std::chrono::steady_clock::time_point start;

    ScopedRenderTimer()
    {
        if constexpr (ShowDebug) start = std::chrono::steady_clock::now();
    }

    ~ScopedRenderTimer()
    {
        if constexpr (!ShowDebug) return;

        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - start).count();

        // Accumulated since this timer's window last flushed, then
        // averaged and reset once a full second has elapsed —
        // function-static, so this persists frame to frame without
        // needing any storage outside this destructor. Distinct per
        // TargetAvg (see class comment above).
        static double                                 s_accumMs     = 0.0;
        static int                                    s_accumCount  = 0;
        static std::chrono::steady_clock::time_point  s_windowStart = now;

        s_accumMs += ms;
        s_accumCount++;

        if (std::chrono::duration<double>(now - s_windowStart).count() >= 1.0)
        {
            TargetAvg     = (float)(s_accumMs / s_accumCount);
            s_accumMs     = 0.0;
            s_accumCount  = 0;
            s_windowStart = now;
        }
    }
};

using RenderTimer        = ScopedRenderTimer<g_AvgRenderTimeMs>;
using OptionsRenderTimer = ScopedRenderTimer<g_AvgOptionsRenderTimeMs>;

using SubsBarDataTimer      = ScopedRenderTimer<g_AvgSubsBarDataMs>;
using SubsBarDrawTimer      = ScopedRenderTimer<g_AvgSubsBarDrawMs>;
using SubsWindowDataTimer   = ScopedRenderTimer<g_AvgSubsWindowDataMs>;
using SubsWindowDrawTimer   = ScopedRenderTimer<g_AvgSubsWindowDrawMs>;
using SubsNotifyDataTimer   = ScopedRenderTimer<g_AvgSubsNotifyDataMs>;
using SubsNotifyDrawTimer   = ScopedRenderTimer<g_AvgSubsNotifyDrawMs>;

// Nexus-required exports
void AddonLoad  (AddonAPI_t* aAPI);
void AddonUnload();
void AddonRender();

// Options panel callback (RT_OptionsRender) — defined in addon_options.cpp
void AddonOptions();