//################################################################################
// background_threads.h
//--------------------------------------------------------------------------------
// BackgroundThreadGuard        RAII marker, counts a thread as "in flight"
// IsShuttingDown()             checkpoint check for a background thread
// RegisterShutdownHook(hook)   register an unload-time interrupt callback
// WaitForBackgroundThreads(ms) call from AddonUnload before touching state
//--------------------------------------------------------------------------------
// Coordination for detached background threads (WinHTTP polling in
// gw2_api.cpp, the keystroke-injection paste sequence in subscriptions.cpp)
// so none of them are still executing this DLL's code after AddonUnload()
// returns and the host (Nexus) FreeLibrary()s the module - which is
// undefined behavior / a crash risk, since a detached std::thread has no
// built-in way to be waited on.
//
// Usage from a thread spawner:
//
//   std::thread([...]()
//   {
//       BackgroundThreadGuard guard; // counts this thread as "in flight"
//       if (IsShuttingDown()) return; // bail before doing any work at all
//       ... do work in a few steps, checking IsShuttingDown() between the
//           expensive/blocking ones so an unload doesn't have to wait for
//           every remaining step to finish ...
//   }).detach();
//
// If a step can block INSIDE a single OS call for a long time with no
// natural checkpoint (e.g. a synchronous WinHTTP request already in
// flight), IsShuttingDown() checkpoints alone can't interrupt it - register
// a shutdown hook (RegisterShutdownHook below) that forces that specific
// call to return early instead.
//
// Usage from AddonUnload, BEFORE touching any state a background thread
// might also touch:
//
//   WaitForBackgroundThreads(2000);
//--------------------------------------------------------------------------------

#pragma once

#include <functional>

//********************************************************************************
// BackgroundThreadGuard
//--------------------------------------------------------------------------------
// RAII marker: increments a live-thread counter on construction, decrements
// it on destruction (including via an early "return" inside the thread
// lambda). WaitForBackgroundThreads polls this counter.
//--------------------------------------------------------------------------------
class BackgroundThreadGuard
{
public:
    BackgroundThreadGuard();
    ~BackgroundThreadGuard();

    BackgroundThreadGuard(const BackgroundThreadGuard&)            = delete;
    BackgroundThreadGuard& operator=(const BackgroundThreadGuard&) = delete;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsShuttingDown
//--------------------------------------------------------------------------------
// Background threads should check this between steps and bail out early
// (returning, so their BackgroundThreadGuard destructs) once it goes true,
// rather than doing any more work whose results will just be discarded.
//--------------------------------------------------------------------------------
bool IsShuttingDown();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RegisterShutdownHook
//--------------------------------------------------------------------------------
// Runs a callback once, at the moment WaitForBackgroundThreads is called
// (after the shutdown flag is set, before the wait loop starts). For a
// subsystem whose background work can be stuck inside a single long/
// uninterruptible blocking OS call - the callback gets a chance to force
// that call to fail/return immediately (e.g. by closing the handle it's
// blocked on) instead of relying on timeoutMs. Safe to call from any
// thread; hooks run on whatever thread calls WaitForBackgroundThreads
// (normally the main/render thread during unload).
//--------------------------------------------------------------------------------
void RegisterShutdownHook(std::function<void()> hook);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitForBackgroundThreads
//--------------------------------------------------------------------------------
// Call exactly once, from AddonUnload, before touching anything a
// background thread might also touch. Sets the shutdown flag, runs every
// registered shutdown hook, then blocks until every live
// BackgroundThreadGuard has been destroyed or timeoutMs elapses.
//
// Kept short: this is a last-resort backstop for a thread that didn't
// respond to the hooks/checkpoints above, not the primary mechanism - a
// long value here would stall the whole game's shutdown if the host calls
// AddonUnload as part of an orderly exit. A hard process kill bypasses
// this function entirely and terminates every thread at once, so this
// wait only matters for "this DLL unloads while the game keeps running."
//--------------------------------------------------------------------------------
void WaitForBackgroundThreads(int timeoutMs);