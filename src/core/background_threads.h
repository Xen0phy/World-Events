//################################################################################
// background_threads.h
//--------------------------------------------------------------------------------
// BackgroundThreadGuard        RAII marker, counts a thread as "in flight"
// IsShuttingDown()             checkpoint check for a background thread
// RegisterShutdownHook(hook)   register an unload-time interrupt callback
// WaitForBackgroundThreads(ms) call from AddonUnload before touching state
//--------------------------------------------------------------------------------
// Coordination for detached background threads (WinHTTP polling in gw2_api.cpp,
// the keystroke-injection paste sequence in subscriptions.cpp) so none of them
// are still executing this DLL's code after AddonUnload() returns and the host
// (Nexus) FreeLibrary()s the module - a detached std::thread has no built-in way
// to be waited on otherwise.
//
// Thread spawner:
//   std::thread([...]() {
//       BackgroundThreadGuard guard;
//       if (IsShuttingDown()) return;
//       ... work in steps, checking IsShuttingDown() between them ...
//   }).detach();
//
// If a single step can block inside one OS call with no natural checkpoint (e.g.
// a synchronous WinHTTP request), register a shutdown hook that forces that call
// to return early instead.
//
// AddonUnload, before touching any state a background thread might also touch:
//   WaitForBackgroundThreads(2000);
//--------------------------------------------------------------------------------

#pragma once

#include <functional>

//********************************************************************************
// BackgroundThreadGuard
//--------------------------------------------------------------------------------
// RAII marker: increments a live-thread counter on construction, decrements it on
// destruction (including via an early "return" inside the thread lambda).
// WaitForBackgroundThreads polls this counter.
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
// (returning, so their BackgroundThreadGuard destructs) once it goes true;
// further work would just be discarded.
//--------------------------------------------------------------------------------
bool IsShuttingDown();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RegisterShutdownHook
//--------------------------------------------------------------------------------
// Runs a callback once, at the moment WaitForBackgroundThreads is called (after
// the shutdown flag is set, before the wait loop starts). For a subsystem whose
// background work can be stuck inside a single long, uninterruptible blocking OS
// call - the callback gets a chance to force that call to fail/return immediately
// (e.g. by closing the handle it's blocked on) instead of relying on timeoutMs.
// Safe to call from any thread; hooks run on whatever thread calls
// WaitForBackgroundThreads (normally the main/render thread during unload).
//--------------------------------------------------------------------------------
void RegisterShutdownHook(std::function<void()> hook);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitForBackgroundThreads
//--------------------------------------------------------------------------------
// Call exactly once, from AddonUnload, before touching anything a background
// thread might also touch. Sets the shutdown flag, runs every registered shutdown
// hook, then blocks until every live BackgroundThreadGuard has been destroyed or
// timeoutMs elapses.
//
// Kept short: a last-resort backstop for a thread that missed the
// hooks/checkpoints above, not the primary mechanism.
//--------------------------------------------------------------------------------
void WaitForBackgroundThreads(int timeoutMs);