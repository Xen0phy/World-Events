//################################################################################
// subscriptions.cpp
//--------------------------------------------------------------------------------
// Storage and JSON persistence for the user's subscribed-events watchlist (see
// subscriptions.h). No compiled-in defaults to merge against - like
// events_categories.cpp, loading just replaces whatever's in memory with
// whatever's on disk. Persisted in events.json alongside "events"/
// "cyclicGroups"/"basicCategories"/"cyclicCategories", as six more top-level
// keys.
//
// The chat-paste helpers (PasteToChat, BuildChatPasteMessage,
// CopyTextToClipboard) also live here, alongside the watchlist storage that feeds
// them.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "background_threads.h"
#include "better_chat.h" //. IsBetterChatSelfCommandEnabled, for PasteToChat's /self fallback
#include <nlohmann/json.hpp>
#include "settings.h"
#include "subscriptions.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<std::string>            g_SubscribedBasicEvents;
std::vector<CyclicSubscriptionKey>  g_SubscribedCyclicSlots;

std::vector<std::string>            g_ToastEnabledBasicEvents;
std::vector<CyclicSubscriptionKey>  g_ToastEnabledCyclicSlots;

std::vector<std::string>            g_SoundEnabledBasicEvents;
std::vector<CyclicSubscriptionKey>  g_SoundEnabledCyclicSlots;

//_ See GetSubscriptionListGeneration's comment in subscriptions.h.
static uint64_t s_subscriptionListGeneration = 0;
uint64_t GetSubscriptionListGeneration() { return s_subscriptionListGeneration; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventSubscribed / ToggleBasicEventSubscription   (see: subscriptions.h)
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenameSubscribedBasicEvent
//--------------------------------------------------------------------------------
// Patches every occurrence in each list, not just the first, in case of a prior
// data inconsistency; see subscriptions.h for what gets patched.
//--------------------------------------------------------------------------------
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName)
{
    if (oldName == newName) return;

    for (auto& name : g_SubscribedBasicEvents)
        if (name == oldName)
            name = newName;

    for (auto& name : g_ToastEnabledBasicEvents)
        if (name == oldName)
            name = newName;

    for (auto& name : g_SoundEnabledBasicEvents)
        if (name == oldName)
            name = newName;

    s_subscriptionListGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotSubscribed / ToggleCyclicSlotSubscription   (see: subscriptions.h)
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearAllSubscriptions   (see: subscriptions.h)
//--------------------------------------------------------------------------------
void ClearAllSubscriptions()
{
    g_SubscribedBasicEvents.clear();
    g_SubscribedCyclicSlots.clear();
    g_ToastEnabledBasicEvents.clear();
    g_ToastEnabledCyclicSlots.clear();
    g_SoundEnabledBasicEvents.clear();
    g_SoundEnabledCyclicSlots.clear();
    s_subscriptionListGeneration++;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventToastEnabled / IsCyclicSlotToastEnabled   (see: subscriptions.h)
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventSoundEnabled / IsCyclicSlotSoundEnabled   (see: subscriptions.h)
//--------------------------------------------------------------------------------
bool IsBasicEventSoundEnabled(const std::string& eventName)
{
    return std::find(g_SoundEnabledBasicEvents.begin(), g_SoundEnabledBasicEvents.end(), eventName)
        != g_SoundEnabledBasicEvents.end();
}

bool IsCyclicSlotSoundEnabled(const CyclicSubscriptionKey& key)
{
    return std::find(g_SoundEnabledCyclicSlots.begin(), g_SoundEnabledCyclicSlots.end(), key)
        != g_SoundEnabledCyclicSlots.end();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SetMembership
//--------------------------------------------------------------------------------
// Makes membership of `value` in `list` match `want`, adding/erasing as needed.
// Local to this file; shared by the two Set...NotifyLevel functions below.
//--------------------------------------------------------------------------------
template <typename T>
static void SetMembership(std::vector<T>& list, const T& value, bool want)
{
    auto it = std::find(list.begin(), list.end(), value);
    bool has = it != list.end();
    if (has == want) return;
    if (want)
        list.push_back(value);
    else
        list.erase(it);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetBasicEventNotifyLevel / SetBasicEventNotifyLevel
//--------------------------------------------------------------------------------
// Derives/sets the ladder described in subscriptions.h. Set clamps level to 0..3,
// then brings subscribed/toast/sound into agreement via
// ToggleBasicEventSubscription (bumps the generation) and SetMembership.
//--------------------------------------------------------------------------------
int GetBasicEventNotifyLevel(const std::string& eventName)
{
    if (!IsBasicEventSubscribed(eventName)) return 0;
    if (!IsBasicEventToastEnabled(eventName)) return 1;
    return IsBasicEventSoundEnabled(eventName) ? 3 : 2;
}

void SetBasicEventNotifyLevel(const std::string& eventName, int level)
{
    level = level < 0 ? 0 : (level > 3 ? 3 : level);
    bool wantSubscribed = level >= 1;
    bool wantToast      = level >= 2;
    bool wantSound      = level >= 3;

    if (IsBasicEventSubscribed(eventName) != wantSubscribed)
        ToggleBasicEventSubscription(eventName);

    SetMembership(g_ToastEnabledBasicEvents, eventName, wantSubscribed && wantToast);
    SetMembership(g_SoundEnabledBasicEvents, eventName, wantSubscribed && wantSound);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetCyclicSlotNotifyLevel / SetCyclicSlotNotifyLevel
//--------------------------------------------------------------------------------
// Same logic as GetBasicEventNotifyLevel/SetBasicEventNotifyLevel, for Cyclic
// slots.
//--------------------------------------------------------------------------------
int GetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key)
{
    if (!IsCyclicSlotSubscribed(key)) return 0;
    if (!IsCyclicSlotToastEnabled(key)) return 1;
    return IsCyclicSlotSoundEnabled(key) ? 3 : 2;
}

void SetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key, int level)
{
    level = level < 0 ? 0 : (level > 3 ? 3 : level);
    bool wantSubscribed = level >= 1;
    bool wantToast      = level >= 2;
    bool wantSound      = level >= 3;

    if (IsCyclicSlotSubscribed(key) != wantSubscribed)
        ToggleCyclicSlotSubscription(key);

    SetMembership(g_ToastEnabledCyclicSlots, key, wantSubscribed && wantToast);
    SetMembership(g_SoundEnabledCyclicSlots, key, wantSubscribed && wantSound);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SerializeCyclicKey / DeserializeCyclicKey
//--------------------------------------------------------------------------------
// Deserialize defaults missing fields to empty string / 0 instead of throwing.
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveSubscriptionsData / LoadSubscriptionsData
//--------------------------------------------------------------------------------
// Save reads the existing events.json first (same pattern as SaveCategoriesData)
// so this only adds/updates the six subscription keys without clobbering the rest
// of the file.
//
// Load defaults any missing key to empty via .value(...), so an events.json from
// before this feature existed loads with every subscription non-toast/non-sound
// instead of opting in silently.
//--------------------------------------------------------------------------------
bool SaveSubscriptionsData(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\events.json";

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
        j["soundEnabledBasicEvents"] = g_SoundEnabledBasicEvents;

        json cyclicArr = json::array();
        for (const auto& key : g_SubscribedCyclicSlots)
            cyclicArr.push_back(SerializeCyclicKey(key));
        j["subscribedCyclicSlots"] = cyclicArr;

        json toastCyclicArr = json::array();
        for (const auto& key : g_ToastEnabledCyclicSlots)
            toastCyclicArr.push_back(SerializeCyclicKey(key));
        j["toastEnabledCyclicSlots"] = toastCyclicArr;

        json soundCyclicArr = json::array();
        for (const auto& key : g_SoundEnabledCyclicSlots)
            soundCyclicArr.push_back(SerializeCyclicKey(key));
        j["soundEnabledCyclicSlots"] = soundCyclicArr;

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
        if (!file.is_open()) return false;   //. no file yet, stays empty

        json j = json::parse(file);

        if (j.contains("subscribedBasicEvents"))
            g_SubscribedBasicEvents = j.value("subscribedBasicEvents", std::vector<std::string>{});

        g_ToastEnabledBasicEvents = j.value("toastEnabledBasicEvents", std::vector<std::string>{});
        g_SoundEnabledBasicEvents = j.value("soundEnabledBasicEvents", std::vector<std::string>{});

        g_SubscribedCyclicSlots.clear();
        if (j.contains("subscribedCyclicSlots") && j["subscribedCyclicSlots"].is_array())
            for (const auto& kj : j["subscribedCyclicSlots"])
                g_SubscribedCyclicSlots.push_back(DeserializeCyclicKey(kj));

        g_ToastEnabledCyclicSlots.clear();
        if (j.contains("toastEnabledCyclicSlots") && j["toastEnabledCyclicSlots"].is_array())
            for (const auto& kj : j["toastEnabledCyclicSlots"])
                g_ToastEnabledCyclicSlots.push_back(DeserializeCyclicKey(kj));

        g_SoundEnabledCyclicSlots.clear();
        if (j.contains("soundEnabledCyclicSlots") && j["soundEnabledCyclicSlots"].is_array())
            for (const auto& kj : j["soundEnabledCyclicSlots"])
                g_SoundEnabledCyclicSlots.push_back(DeserializeCyclicKey(kj));

        s_subscriptionListGeneration++;
        return true;
    }
    catch (...) { return false; }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CopyTextToClipboard
//--------------------------------------------------------------------------------
// Plain Win32 clipboard write. No synthetic keystrokes, no window-handle
// targeting, nothing sent to the game process - this only touches the shared OS
// clipboard, same as any other app's "Copy" button.
//--------------------------------------------------------------------------------
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// get_l_param
//--------------------------------------------------------------------------------
// Builds the LPARAM Windows expects for a synthesized WM_KEYDOWN/WM_KEYUP
// message: scan code in bits 16-23, repeat/previous-state and transition- state
// flags in the high bits. See MSDN's WM_KEYDOWN/WM_KEYUP docs for the full bit
// layout.
//--------------------------------------------------------------------------------
LPARAM get_l_param(std::uint32_t key, bool down, bool repeat = false)
{
    std::uint32_t scan_code = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);

    std::uint32_t l_param = 1;                    //. repeat count, bits 0-15
    l_param |= (scan_code & 0xFF) << 16;           //. bits 16-23
    //_ Bit 24 (extended key flag) and bits 25-28 (reserved) are left 0; set bit 24 via (is_extended_key(key)?1u:0u)<<24 if ever needed.
    l_param |= (down && repeat ? 1u : 0u) << 30;   //. previous key state
    l_param |= (!down ? 1u : 0u) << 31;            //. transition state

    return static_cast<LPARAM>(l_param);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// BuildChatPasteMessage   (pairs with: PasteToChat)
//--------------------------------------------------------------------------------
// Builds "<name>: <chatCode>" (or just <name>) for a watchlist row/segment/toast
// click. Unprefixed - PasteToChat applies Settings::ChatChannelPrefix itself,
// since /w pastes the prefix and body into two different input boxes instead of
// one concatenated string.
//--------------------------------------------------------------------------------
std::string BuildChatPasteMessage(const std::string& name, const std::string& chatCode)
{
    return chatCode.empty() ? name : (name + ": " + chatCode);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetMumbleCharacterName
//--------------------------------------------------------------------------------
// Mumble::Data::Identity (Mumble.h) is a UTF-16 JSON string, not a plain name
// field - {"name":"...", "profession":N, ...}. Narrowed to UTF-8 to parse with
// nlohmann_json, then narrowed again to the clipboard's ANSI codepage so accented
// names survive CopyTextToClipboard's CF_TEXT write the same as any other pasted
// segment. Returns empty on any failure: MumbleLink not ready yet,
// malformed/empty identity, missing "name".
//--------------------------------------------------------------------------------
std::string GetMumbleCharacterName()
{
    if (!MumbleLink || MumbleLink->Identity[0] == L'\0')
        return "";

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0)
        return "";
    std::string utf8Identity(utf8Len - 1, '\0');   //. -1 drops the counted null terminator
    WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1, utf8Identity.data(), utf8Len, nullptr, nullptr);

    std::string name;
    try { name = json::parse(utf8Identity).value("name", ""); }
    catch (...) { return ""; }

    if (name.empty())
        return "";

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    if (wideLen <= 0)
        return "";
    std::wstring wideName(wideLen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wideName.data(), wideLen);

    int ansiLen = WideCharToMultiByte(CP_ACP, 0, wideName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ansiLen <= 0)
        return "";
    std::string ansiName(ansiLen - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, wideName.c_str(), -1, ansiName.data(), ansiLen, nullptr, nullptr);

    return ansiName;
}

//_ Guards PasteSegmentsToChat below against overlapping calls.
std::atomic<bool> send_in_progress{false};

//********************************************************************************
// ChatPasteSegment
//--------------------------------------------------------------------------------
// text        clipboard payload for this segment
// tabAfter    press Tab after pasting, before moving to the next segment
//--------------------------------------------------------------------------------
// One clipboard-load-and-paste step in a PasteSegmentsToChat sequence. Most
// channels need a single segment; /w's two-input-box layout needs three (see
// PasteToChat).
//--------------------------------------------------------------------------------
struct ChatPasteSegment
{
    std::string text;
    bool        tabAfter = false;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PasteSegmentsToChat   (pairs with: PasteToChat)
//--------------------------------------------------------------------------------
// Mixes two input mechanisms: Enter and 'V' go through SendMessage (posts
// straight to the target window's queue), while Ctrl goes through SendInput (the
// real, system-wide input stream GetKeyState reads) - required for the third-
// party target app this talks to, since an all-SendInput or all- SendMessage
// version both failed to deliver a recognized Ctrl+V there. Runs on a detached
// background thread guarded by BackgroundThreadGuard (background_threads.h) so
// AddonUnload can wait for it to finish; IsShuttingDown() is checked between
// steps so it can bail early, releasing Ctrl first if it was already held.
//--------------------------------------------------------------------------------
void PasteSegmentsToChat(std::vector<ChatPasteSegment> segments, std::chrono::milliseconds delay_ms)
{
    if (segments.empty())
        return;

    bool expected = false;
    if (!send_in_progress.compare_exchange_strong(expected, true))
        return;   //. already sending, ignore this click

    HWND tool_handle = GetForegroundWindow();
    CopyTextToClipboard(segments.front().text);
    std::thread(
        [segments = std::move(segments), delay_ms, tool_handle]()
        {
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

            for (size_t i = 0; i < segments.size(); i++)
            {
                if (IsShuttingDown()) { send_in_progress.store(false); return; }

                if (i > 0)
                    CopyTextToClipboard(segments[i].text);   //. segment 0 was already loaded before the thread started

                INPUT in{};
                in.type = INPUT_KEYBOARD;
                in.ki.wVk = VK_CONTROL;
                SendInput(1, &in, sizeof(INPUT));   //. Ctrl down

                std::this_thread::sleep_for(delay_ms);

                if (IsShuttingDown())
                {
                    in.ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(1, &in, sizeof(INPUT));   //. release Ctrl before bailing
                    send_in_progress.store(false);
                    return;
                }

                //_ WM_PASTE was tried here directly but doesn't reliably reach the third-party target app - hence Ctrl+V key events.
                SendMessage(tool_handle, WM_KEYDOWN, 'V', get_l_param('V', true));
                SendMessage(tool_handle, WM_KEYUP, 'V', get_l_param('V', false));
                std::this_thread::sleep_for(delay_ms);

                in.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &in, sizeof(INPUT));   //. Ctrl up

                std::this_thread::sleep_for(delay_ms);

                if (segments[i].tabAfter)
                {
                    if (IsShuttingDown()) { send_in_progress.store(false); return; }

                    SendMessage(tool_handle, WM_KEYDOWN, VK_TAB, get_l_param(VK_TAB, true));
                    SendMessage(tool_handle, WM_KEYUP, VK_TAB, get_l_param(VK_TAB, false));
                    std::this_thread::sleep_for(delay_ms);
                }
            }

            if (IsShuttingDown()) { send_in_progress.store(false); return; }

            SendMessage(tool_handle, WM_KEYDOWN, VK_RETURN, get_l_param(VK_RETURN, true));
            SendMessage(tool_handle, WM_KEYUP, VK_RETURN, get_l_param(VK_RETURN, false));
            std::this_thread::sleep_for(delay_ms);

            send_in_progress.store(false);   //. release the guard when done
        }
    )
    .detach();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PasteToChat   (pairs with: BuildChatPasteMessage, PasteSegmentsToChat)
//--------------------------------------------------------------------------------
// Applies Settings::ChatChannelPrefix to message, then hands it to
// PasteSegmentsToChat. Every prefix but "/w " pastes as one segment: prefix and
// body concatenated. "/w " needs GW2's own whisper box, which takes the target
// name and message in two separate fields reached by pressing Tab between them,
// so that case pastes three segments instead - "/w ", the Mumble-reported
// character name, then message - with Tab after the name; an unreadable
// character name drops the whisper instead of sending it to whatever box has
// focus. "/self " falls back to the unprefixed default the same way when Better
// Chat's /self command isn't available, since a stale saved setting must not
// paste a dead command into whatever box has focus.
//--------------------------------------------------------------------------------
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms)
{
    if (ChatChannelPrefix == "/w ")
    {
        std::string charName = GetMumbleCharacterName();
        if (charName.empty())
            return;

        PasteSegmentsToChat(
            {
                { "/w ",    false },
                { charName, true  },
                { message,  false }
            },
            delay_ms);
        return;
    }

    if (ChatChannelPrefix == "/self " && !IsBetterChatSelfCommandEnabled())
    {
        PasteSegmentsToChat({ { message, false } }, delay_ms);
        return;
    }

    PasteSegmentsToChat({ { ChatChannelPrefix + message, false } }, delay_ms);
}