//################################################################################
// background_threads.cpp
//--------------------------------------------------------------------------------
// See background_threads.h for the overall design/rationale.
//--------------------------------------------------------------------------------

#include "background_threads.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

static std::atomic<int>  s_activeThreads{0};      //. live BackgroundThreadGuard count
static std::atomic<bool> s_shuttingDown{false};   //. set once by WaitForBackgroundThreads

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// HooksMutex / ShutdownHooks
//--------------------------------------------------------------------------------
// Function-local statics, not file-scope globals: gw2_api.cpp registers its
// cancellation hook via a file-scope static initializer of its own, and C++ gives
// no ordering guarantee between two different translation units' file- scope
// statics. A function-local static is guaranteed (thread-safely, since C++11) to
// construct on first use regardless of which TU's static initializers ran first,
// so RegisterShutdownHook is safe to call from another module's static
// initializer, which a file-scope vector here would not be.
//--------------------------------------------------------------------------------
static std::mutex& HooksMutex()
{
    static std::mutex m;
    return m;
}
static std::vector<std::function<void()>>& ShutdownHooks()
{
    static std::vector<std::function<void()>> hooks;
    return hooks;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BackgroundThreadGuard::BackgroundThreadGuard / ~BackgroundThreadGuard   (see: background_threads.h)
//--------------------------------------------------------------------------------
BackgroundThreadGuard::BackgroundThreadGuard()
{
    s_activeThreads.fetch_add(1, std::memory_order_relaxed);
}

BackgroundThreadGuard::~BackgroundThreadGuard()
{
    s_activeThreads.fetch_sub(1, std::memory_order_relaxed);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsShuttingDown   (see: background_threads.h)
//--------------------------------------------------------------------------------
bool IsShuttingDown()
{
    return s_shuttingDown.load(std::memory_order_relaxed);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RegisterShutdownHook   (see: background_threads.h)
//--------------------------------------------------------------------------------
void RegisterShutdownHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(HooksMutex());
    ShutdownHooks().push_back(std::move(hook));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WaitForBackgroundThreads
//--------------------------------------------------------------------------------
// A long timeout here would stall the whole game's shutdown if the host calls
// AddonUnload as part of an orderly exit. A hard process kill bypasses this
// function and terminates every thread at once, so the wait only matters when the
// DLL unloads while the game keeps running. Threads still active past the
// deadline are simply left running - AddonUnload has to return eventually.
//--------------------------------------------------------------------------------
void WaitForBackgroundThreads(int timeoutMs)
{
    s_shuttingDown.store(true, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(HooksMutex());
        for (auto& hook : ShutdownHooks())
            //_ e.g. force an in-flight WinHTTP call to return early
            hook();
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (s_activeThreads.load(std::memory_order_relaxed) > 0 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}