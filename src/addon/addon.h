//################################################################################
// addon.h
//--------------------------------------------------------------------------------
// APIDefs/MumbleLink/NexusLink   Nexus API pointers, valid after AddonLoad
// g_AddonDir                     addon's own data directory
// ShowDebug                      build-time debug/timing switch
// ScopedRenderTimer<T>           RAII per-frame timer, debug-only
// AddonLoad/AddonUnload/AddonRender   Nexus-required exports (see addon.cpp)
// AddonOptions                   options panel callback (addon_options.cpp)
//--------------------------------------------------------------------------------

#pragma once

#include "Mumble.h"
#include "Nexus.h"

#include <chrono>
#include <string>

//_ Set in AddonLoad, valid until AddonUnload.
extern AddonAPI_t*      APIDefs;
extern Mumble::Data*    MumbleLink;
extern NexusLinkData_t* NexusLink;

//_ Set once in AddonLoad via APIDefs->Paths_GetAddonDirectory. Used for
// settings.ini and any future JSON data files.
extern std::string g_AddonDir;

//_ Build-time only, deliberately not a settings_table.h SETTING - flip
// and rebuild rather than exposing as a real user-facing option.
inline constexpr bool ShowDebug = false;

//_ Rolling ~1s average, ms, of AddonRender's own body (see
// ScopedRenderTimer below). 0 unless ShowDebug is true.
extern float g_AvgRenderTimeMs;

//_ Same idea as g_AvgRenderTimeMs, but split per view (Bar/Window/Notify)
// and phase: "Data" is gathering what to show, "Draw" is the ImGui calls.
extern float g_AvgSubsBarDataMs,      g_AvgSubsBarDrawMs;
extern float g_AvgSubsWindowDataMs,   g_AvgSubsWindowDrawMs;
extern float g_AvgSubsNotifyDataMs,   g_AvgSubsNotifyDrawMs;

//_ Same idea as g_AvgRenderTimeMs, but for AddonOptions's own body - kept
// separate since rebuilding the options panel is a distinct render cost.
extern float g_AvgOptionsRenderTimeMs;

//********************************************************************************
// ScopedRenderTimer<TargetAvg>
//--------------------------------------------------------------------------------
// start   time point captured on construction (ShowDebug only)
//--------------------------------------------------------------------------------
// RAII scope timer, templated on WHICH global accumulator it feeds. Every
// distinct template argument is a distinct type, so RenderTimer and
// OptionsRenderTimer below each get their own destructor and therefore
// their own function-local static accumulator state - instantiating both
// never mixes their totals, unlike a single shared non-template timer
// would if used at two call sites.
//
// if constexpr on ShowDebug means this compiles down to an empty
// constructor/destructor when ShowDebug is false, so leaving these in
// place has no runtime cost in a normal build.
//--------------------------------------------------------------------------------
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

        //_ Accumulated since this timer's window last flushed, averaged
        // and reset once a second has elapsed; function-static so this
        // persists frame to frame. Distinct per TargetAvg (see above).
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonLoad / AddonUnload / AddonRender
//--------------------------------------------------------------------------------
// Nexus-required exports (see GetAddonDef in addon.cpp) - called on
// load/unload and once per frame respectively.
//--------------------------------------------------------------------------------
void AddonLoad  (AddonAPI_t* aAPI);
void AddonUnload();
void AddonRender();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonOptions
//--------------------------------------------------------------------------------
// Options panel callback (RT_OptionsRender) - implemented in
// addon_options.cpp, not addon.cpp.
//--------------------------------------------------------------------------------
void AddonOptions();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ResetAllDataToDefaults
//--------------------------------------------------------------------------------
// "Default" button in the options panel's Events tab (addon_options.cpp):
// deletes events.json outright and rebuilds every list it holds - events,
// cyclicGroups, categories, subscriptions/toast/sound opt-ins, and daily
// done-today markers - back to a clean compiled-in slate, then writes a
// fresh file. Returns false if the on-disk delete failed (everything in
// memory still gets reset either way).
//--------------------------------------------------------------------------------
bool ResetAllDataToDefaults();