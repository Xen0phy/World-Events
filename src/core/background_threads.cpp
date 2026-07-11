// background_threads.cpp
// See background_threads.h for the overall design/rationale.

#include "background_threads.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>

static std::atomic<int>  s_activeThreads{0};
static std::atomic<bool> s_shuttingDown{false};

// Function-local statics (not file-scope globals) deliberately — gw2_api.cpp
// registers its cancellation hook via a file-scope static initializer of
// its own, and C++ gives no guarantee about initialization order BETWEEN
// two different translation units' file-scope statics. A function-local
// static is guaranteed (thread-safely, since C++11) to be constructed the
// first time control reaches it, regardless of which TU's static
// initializers happened to run first — so RegisterShutdownHook is safe to
// call from another module's static initializer, which a file-scope vector
// here would not be.
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

BackgroundThreadGuard::BackgroundThreadGuard()
{
    s_activeThreads.fetch_add(1, std::memory_order_relaxed);
}

BackgroundThreadGuard::~BackgroundThreadGuard()
{
    s_activeThreads.fetch_sub(1, std::memory_order_relaxed);
}

bool IsShuttingDown()
{
    return s_shuttingDown.load(std::memory_order_relaxed);
}

void RegisterShutdownHook(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(HooksMutex());
    ShutdownHooks().push_back(std::move(hook));
}

void WaitForBackgroundThreads(int timeoutMs)
{
    s_shuttingDown.store(true, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(HooksMutex());
        for (auto& hook : ShutdownHooks())
            hook(); // e.g. force an in-flight WinHTTP call to return early
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (s_activeThreads.load(std::memory_order_relaxed) > 0 &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // If threads are still active past the deadline, there is nothing safe
    // left to do from here — AddonUnload still has to return eventually.
    // This is a last-resort bound, not a promise; the shutdown hook(s) and
    // IsShuttingDown() checkpoints above are what make hitting it unlikely.
}
