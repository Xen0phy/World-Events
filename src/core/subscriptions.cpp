// subscriptions.cpp
// Storage and JSON persistence for the user's subscribed-events watchlist.
//
// Mirrors events_categories.cpp closely: no compiled-in defaults to merge
// against, so loading just replaces whatever's in memory with whatever's
// on disk. Persisted in events.json alongside "events"/"cyclicGroups"/
// "basicCategories"/"cyclicCategories", as two more sibling keys.

#include "subscriptions.h"
#include "addon.h"
#include "background_threads.h"
#include "settings.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
#include <windows.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<std::string>            g_SubscribedBasicEvents;
std::vector<CyclicSubscriptionKey>  g_SubscribedCyclicSlots;

std::vector<std::string>            g_ToastEnabledBasicEvents;
std::vector<CyclicSubscriptionKey>  g_ToastEnabledCyclicSlots;

// See GetSubscriptionListGeneration's comment in subscriptions.h.
static uint64_t s_subscriptionListGeneration = 0;
uint64_t GetSubscriptionListGeneration() { return s_subscriptionListGeneration; }

// ---------------------------------------------------------------------------
// Basic Event subscriptions
// ---------------------------------------------------------------------------
bool IsBasicEventSubscribed(const std::string& eventName)
{
    return std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), eventName)
        != g_SubscribedBasicEvents.end();
}

void ToggleBasicEventSubscription(const std::string& eventName)
{
    auto it = std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), eventName);
    if (it != g_SubscribedBasicEvents.end())
        g_SubscribedBasicEvents.erase(it);
    else
        g_SubscribedBasicEvents.push_back(eventName);
    s_subscriptionListGeneration++;
}

void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName)
{
    if (oldName == newName) return;

    for (auto& name : g_SubscribedBasicEvents)
        if (name == oldName)
            name = newName;
        // Deliberately no break: patch every occurrence, not just the
        // first, in case of a prior data inconsistency.

    // Same patch, same reasoning, for the toast-enabled list — otherwise
    // a rename would silently drop a "notify" setting the user already
    // configured for this event, with nothing in the UI hinting why.
    for (auto& name : g_ToastEnabledBasicEvents)
        if (name == oldName)
            name = newName;

    s_subscriptionListGeneration++;
}

// ---------------------------------------------------------------------------
// Cyclic slot subscriptions
// ---------------------------------------------------------------------------
bool IsCyclicSlotSubscribed(const CyclicSubscriptionKey& key)
{
    return std::find(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(), key)
        != g_SubscribedCyclicSlots.end();
}

void ToggleCyclicSlotSubscription(const CyclicSubscriptionKey& key)
{
    auto it = std::find(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(), key);
    if (it != g_SubscribedCyclicSlots.end())
        g_SubscribedCyclicSlots.erase(it);
    else
        g_SubscribedCyclicSlots.push_back(key);
    s_subscriptionListGeneration++;
}

// ---------------------------------------------------------------------------
// Per-event toast opt-in ("notify level") — see subscriptions.h
// ---------------------------------------------------------------------------
bool IsBasicEventToastEnabled(const std::string& eventName)
{
    return std::find(g_ToastEnabledBasicEvents.begin(), g_ToastEnabledBasicEvents.end(), eventName)
        != g_ToastEnabledBasicEvents.end();
}

bool IsCyclicSlotToastEnabled(const CyclicSubscriptionKey& key)
{
    return std::find(g_ToastEnabledCyclicSlots.begin(), g_ToastEnabledCyclicSlots.end(), key)
        != g_ToastEnabledCyclicSlots.end();
}

int GetBasicEventNotifyLevel(const std::string& eventName)
{
    if (!IsBasicEventSubscribed(eventName)) return 0;
    return IsBasicEventToastEnabled(eventName) ? 2 : 1;
}

void SetBasicEventNotifyLevel(const std::string& eventName, int level)
{
    level = level < 0 ? 0 : (level > 2 ? 2 : level);
    bool wantSubscribed = level >= 1;
    bool wantToast      = level >= 2;

    if (IsBasicEventSubscribed(eventName) != wantSubscribed)
        ToggleBasicEventSubscription(eventName); // bumps s_subscriptionListGeneration

    // Unsubscribing always clears toast too, regardless of what it was —
    // no way to end up "toast-enabled but not subscribed" left over.
    if (!wantSubscribed)
    {
        auto it = std::find(g_ToastEnabledBasicEvents.begin(), g_ToastEnabledBasicEvents.end(), eventName);
        if (it != g_ToastEnabledBasicEvents.end())
            g_ToastEnabledBasicEvents.erase(it);
        return;
    }

    bool hasToast = IsBasicEventToastEnabled(eventName);
    if (hasToast == wantToast) return;

    if (wantToast)
        g_ToastEnabledBasicEvents.push_back(eventName);
    else
    {
        auto it = std::find(g_ToastEnabledBasicEvents.begin(), g_ToastEnabledBasicEvents.end(), eventName);
        if (it != g_ToastEnabledBasicEvents.end())
            g_ToastEnabledBasicEvents.erase(it);
    }
}

int GetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key)
{
    if (!IsCyclicSlotSubscribed(key)) return 0;
    return IsCyclicSlotToastEnabled(key) ? 2 : 1;
}

void SetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key, int level)
{
    level = level < 0 ? 0 : (level > 2 ? 2 : level);
    bool wantSubscribed = level >= 1;
    bool wantToast      = level >= 2;

    if (IsCyclicSlotSubscribed(key) != wantSubscribed)
        ToggleCyclicSlotSubscription(key); // bumps s_subscriptionListGeneration

    if (!wantSubscribed)
    {
        auto it = std::find(g_ToastEnabledCyclicSlots.begin(), g_ToastEnabledCyclicSlots.end(), key);
        if (it != g_ToastEnabledCyclicSlots.end())
            g_ToastEnabledCyclicSlots.erase(it);
        return;
    }

    bool hasToast = IsCyclicSlotToastEnabled(key);
    if (hasToast == wantToast) return;

    if (wantToast)
        g_ToastEnabledCyclicSlots.push_back(key);
    else
    {
        auto it = std::find(g_ToastEnabledCyclicSlots.begin(), g_ToastEnabledCyclicSlots.end(), key);
        if (it != g_ToastEnabledCyclicSlots.end())
            g_ToastEnabledCyclicSlots.erase(it);
    }
}

// ---------------------------------------------------------------------------
// (De)serialization
// ---------------------------------------------------------------------------
static json SerializeCyclicKey(const CyclicSubscriptionKey& key)
{
    json j;
    j["groupName"]  = key.groupName;
    j["slotOffset"] = key.slotOffset;
    return j;
}

static CyclicSubscriptionKey DeserializeCyclicKey(const json& j)
{
    CyclicSubscriptionKey key;
    key.groupName  = j.value("groupName", std::string());
    key.slotOffset = j.value("slotOffset", 0);
    return key;
}

// ---------------------------------------------------------------------------
// SaveSubscriptionsData / LoadSubscriptionsData
// ---------------------------------------------------------------------------
bool SaveSubscriptionsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

        // Read whatever's already there first (events/cyclicGroups/
        // categories/data_version), so this save only adds/updates the
        // subscription keys without clobbering the rest of the file —
        // same pattern as SaveCategoriesData, and for the same reason.
        json j;
        {
            std::ifstream in(filepath);
            if (in.is_open())
            {
                try { j = json::parse(in); }
                catch (...) { j = json::object(); }
            }
        }

        j["subscribedBasicEvents"] = g_SubscribedBasicEvents;
        j["toastEnabledBasicEvents"] = g_ToastEnabledBasicEvents;

        json cyclicArr = json::array();
        for (const auto& key : g_SubscribedCyclicSlots)
            cyclicArr.push_back(SerializeCyclicKey(key));
        j["subscribedCyclicSlots"] = cyclicArr;

        json toastCyclicArr = json::array();
        for (const auto& key : g_ToastEnabledCyclicSlots)
            toastCyclicArr.push_back(SerializeCyclicKey(key));
        j["toastEnabledCyclicSlots"] = toastCyclicArr;

        fs::create_directories(addonDir);
        std::ofstream out(filepath);
        if (!out.is_open()) return false;
        out << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadSubscriptionsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false; // no file yet — subscriptions stay empty

        json j = json::parse(file);

        if (j.contains("subscribedBasicEvents"))
            g_SubscribedBasicEvents = j.value("subscribedBasicEvents", std::vector<std::string>{});

        // .value(...) defaults to empty if the key is missing entirely —
        // true for any events.json saved before this feature existed, and
        // that's exactly the right behavior: nobody's prior subscriptions
        // silently start out toast-enabled.
        g_ToastEnabledBasicEvents = j.value("toastEnabledBasicEvents", std::vector<std::string>{});

        g_SubscribedCyclicSlots.clear();
        if (j.contains("subscribedCyclicSlots") && j["subscribedCyclicSlots"].is_array())
            for (const auto& kj : j["subscribedCyclicSlots"])
                g_SubscribedCyclicSlots.push_back(DeserializeCyclicKey(kj));

        g_ToastEnabledCyclicSlots.clear();
        if (j.contains("toastEnabledCyclicSlots") && j["toastEnabledCyclicSlots"].is_array())
            for (const auto& kj : j["toastEnabledCyclicSlots"])
                g_ToastEnabledCyclicSlots.push_back(DeserializeCyclicKey(kj));

        s_subscriptionListGeneration++;
        return true;
    }
    catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// CopyTextToClipboard
// ---------------------------------------------------------------------------
// Plain Win32 clipboard write. No synthetic keystrokes, no
// window-handle targeting, nothing sent to the game process; this only
// touches the shared OS clipboard, same as any other app's "Copy" button.
// ---------------------------------------------------------------------------
bool CopyTextToClipboard(const std::string& text)
{
    if (!OpenClipboard(nullptr))
        return false;

    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hMem)
    {
        CloseClipboard();
        return false;
    }

    void* pMem = GlobalLock(hMem);
    memcpy(pMem, text.c_str(), text.size() + 1);
    GlobalUnlock(hMem);

    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return true;
}

LPARAM get_l_param(std::uint32_t key, bool down, bool repeat = false)
{
    std::uint32_t scan_code = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);

    std::uint32_t l_param = 1; // repeat count, bits 0-15 (almost always 1)
    l_param |= (scan_code & 0xFF) << 16; // bits 16-23
    // bit 24: extended key flag - set if needed, e.g.:
    // l_param |= (is_extended_key(key) ? 1u : 0u) << 24;
    // bits 25-28: reserved, leave as 0
    // bit 29: context code - 0 for normal key events
    l_param |= (down && repeat ? 1u : 0u) << 30; // previous key state
    l_param |= (!down ? 1u : 0u) << 31;           // transition state

    return static_cast<LPARAM>(l_param);
}

// NOTE ON MIXED SendMessage/SendInput USAGE (do not "clean this up"):
//
// This function intentionally mixes two different input delivery mechanisms:
//   - Enter and 'V' are delivered via SendMessage(tool_handle, WM_KEYDOWN/WM_KEYUP, ...)
//   - Ctrl is delivered via SendInput(...)
//
// SendMessage posts directly to the target window's message queue, bypassing the
// OS-level input pipeline. SendInput injects into the real, system-wide input stream
// that Windows uses to track actual keyboard/modifier state (GetKeyState, etc).
// Mixing them is normally fragile: an app *could* receive the WM_KEYDOWN for 'V' via
// SendMessage without ever seeing Ctrl as "down" at the OS level, since that Ctrl
// state only exists in the SendInput-driven input stream, not the message queue.
//
// This specific combination is required for the third-party target app this talks
// to: an all-SendInput version and an all-SendMessage version both fail to deliver
// a recognized Ctrl+V there. Do not unify this into a single mechanism without
// re-testing against that actual target application — a "theoretically cleaner"
// version can fail silently (no errors, just no input arriving) rather than
// obviously breaking.
std::string BuildChatPasteMessage(const std::string& name, const std::string& chatCode)
{
    std::string body = chatCode.empty() ? name : (name + ": " + chatCode);
    return ChatChannelPrefix + body; // empty prefix (default) leaves body untouched
}

std::atomic<bool> send_in_progress{false};
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms)
{
    bool expected = false;
    if (!send_in_progress.compare_exchange_strong(expected, true))
    {
        // Already sending — ignore this click rather than overlap
        return;
    }
    HWND tool_handle = GetForegroundWindow();
    CopyTextToClipboard(message);
    std::thread(
        [delay_ms, tool_handle]()
        {
            // Registered for this thread's whole lifetime (every early
            // "return" below included) so AddonUnload's
            // WaitForBackgroundThreads can wait for it to actually finish
            // instead of the DLL potentially being unloaded mid-sequence.
            // See background_threads.h.
            BackgroundThreadGuard threadGuard;

            if (IsShuttingDown())
            {
                send_in_progress.store(false);
                return;
            }

            if (MumbleLink->Context.IsTextboxFocused != 1)
            {
                SendMessage(tool_handle, WM_KEYDOWN, VK_RETURN, get_l_param(VK_RETURN, true));
                SendMessage(tool_handle, WM_KEYUP, VK_RETURN, get_l_param(VK_RETURN, false));
                std::this_thread::sleep_for(delay_ms);
            }

            if (IsShuttingDown()) { send_in_progress.store(false); return; }

            // Ctrl down
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = VK_CONTROL;
            SendInput(1, &in, sizeof(INPUT));
            
            std::this_thread::sleep_for(delay_ms);

            if (IsShuttingDown())
            {
                // Ctrl is physically "down" as far as the OS input stream
                // is concerned — release it before bailing so an unload
                // mid-sequence can't leave a stuck modifier key behind.
                in.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &in, sizeof(INPUT));
                send_in_progress.store(false);
                return;
            }
            
            //SendMessage(tool_handle, WM_PASTE, 0, 0); not working
            SendMessage(tool_handle, WM_KEYDOWN, 'V', get_l_param('V', true));
            SendMessage(tool_handle, WM_KEYUP, 'V', get_l_param('V', false));
            std::this_thread::sleep_for(delay_ms);
            
            // Ctrl up
            in.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &in, sizeof(INPUT));
            
            std::this_thread::sleep_for(delay_ms);

            if (IsShuttingDown()) { send_in_progress.store(false); return; }

            SendMessage(tool_handle, WM_KEYDOWN, VK_RETURN, get_l_param(VK_RETURN, true));
            SendMessage(tool_handle, WM_KEYUP, VK_RETURN, get_l_param(VK_RETURN, false));
            std::this_thread::sleep_for(delay_ms);

            send_in_progress.store(false); // release the guard when done
        }
    )
    .detach();
}