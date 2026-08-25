//################################################################################
// subscriptions_notification.cpp
//--------------------------------------------------------------------------------
// SpawnPopup               pushes one new toast onto the popup stack
// CollectCandidates        (pairs with: UpdateNotifyStates)
// UpdateNotifyStates       (pairs with: CollectCandidates)
// DrawAndExpirePopups      draws/ages/dismisses the popup stack
// RenderSubscriptionsNotifications   public entry point, called once per frame
//--------------------------------------------------------------------------------
// Toast-style popup notifications for subscribed Basic Events / Cyclic slots,
// plus auto-tracked active-and-incomplete weekly Wizard's Vault targets
// (weekly_vault.h) not already manually subscribed. A fourth view of the same
// subscription data as subscriptions_window.cpp / subscriptions_bar.cpp - see
// subscriptions.h.
//
// Fires two independent popups per occurrence: "starting soon"
// (NotificationLeadMinutes before start) and "now active" (on start), both gated
// behind NotificationsEnabled and a per-event toast/sound opt-in for manually
// subscribed items (see Candidate's own comment for that ladder). A weekly-auto-
// track-only candidate isn't part of that opt-in and keeps its toast-only-
// unconditional behavior.
//
// Popups stack in the lower-right corner, newest closest to the corner, auto-
// dismissing after NotificationDisplaySeconds (plus a short fade), and paste the
// same "<name>: <chatCode>" text on click as a row in the watchlist window / a
// segment on the distribution bar.
//--------------------------------------------------------------------------------

//_ SubsNotifyDataTimer/SubsNotifyDrawTimer are declared here, see their comment in addon.h
#include "addon.h"
#include "color_utils.h"
#include "events_tracking.h"
#include "imgui.h"
#include "notify_sound.h"
#include "settings.h"
#include "subscriptions.h"
#include "subscriptions_cache.h"
#include "subscriptions_edit_window.h"
#include "subscriptions_ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

//_ Popup stack layout, screen-space pixels.
static constexpr float kPopupWidth   = 300.0f;
static constexpr float kPopupHeight  = 56.0f;
static constexpr float kPopupGapY    = 8.0f;   //. vertical gap between stacked popups
static constexpr float kMarginX      = 20.0f;  //. from the right screen edge
static constexpr float kMarginY      = 20.0f;  //. from the bottom screen edge
static constexpr float kAccentWidth  = 4.0f;   //. colored left-edge stripe width

//_ Popups older than NotificationDisplaySeconds fade out over this many ms before being removed instead of just vanishing.
static constexpr unsigned long long kFadeOutMs = 400;

//_ Quick fade-in so a freshly-spawned popup doesn't hard-cut into view.
static constexpr unsigned long long kFadeInMs = 150;

//_ Hard cap on visible popups; the oldest is dropped if more arrive before it finishes dismissing.
static constexpr int kMaxVisiblePopups = 4;

//********************************************************************************
// Popup
//--------------------------------------------------------------------------------
// key            stable identity, matches NotifyState's map key; used only
//                for a stable ImGui window id
// name           display name, e.g. "Tequatl the Sunless"
// chatCode       waypoint chat code pasted on click
// message        e.g. "Starting in 4m 32s" or "Now active!"
// color          plain ImVec4 RGBA - SubscriptionsSoonColor for the lead
//                popup, SubscriptionsActiveColor for the start popup
// spawnedAtMs    GetTickCount64() timestamp, doubles as a pause mechanism
// isWeekly       active-and-incomplete weekly Wizard's Vault target
// isBasic, basicName, cyclicKey   identity for the right-click "Mark done
//                for today" menu, mirroring Candidate's own copy
//--------------------------------------------------------------------------------
// While the mouse hovers a popup, DrawAndExpirePopups nudges spawnedAtMs forward
// frame-by-frame instead of letting it age, so a popup the user is actively
// reading/about to click can't expire out from under the cursor. isWeekly draws
// an additional thin red border, the same "counts toward this week's Wizard's
// Vault objective" marker the window's red dot / bar's red dot convey elsewhere;
// purely visual, no effect on click/dismiss. color is a plain ImVec4 instead of a
// packed RGBA + separate alpha override: applying the fade is just `c.w *= alpha`
// at the one call site that needs it (ThemeColorU32/FadeU32, color_utils.h,
// shared with subscriptions_bar.cpp), no conversion helper required.
//--------------------------------------------------------------------------------
struct Popup
{
    std::string key;
    std::string name;
    std::string chatCode;
    std::string message;
    ImVec4 color;
    unsigned long long spawnedAtMs;
    bool isWeekly = false;

    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

static std::vector<Popup> s_popups;   //. active/fading toast stack

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SpawnPopup
//--------------------------------------------------------------------------------
// Builds and pushes one new Popup. Drops the OLDEST entry instead of refusing the
// new one if this exceeds kMaxVisiblePopups - the new arrival is by definition
// the most time-sensitive popup right now.
//--------------------------------------------------------------------------------
static void SpawnPopup(const std::string& key, const std::string& name, const std::string& chatCode,
                        const std::string& message, const ImVec4& color, bool isWeekly,
                        bool isBasic, const std::string& basicName, const CyclicSubscriptionKey& cyclicKey)
{
    Popup p;
    p.key          = key;
    p.name         = name;
    p.chatCode     = chatCode;
    p.message      = message;
    p.color        = color;
    p.spawnedAtMs  = GetTickCount64();
    p.isWeekly     = isWeekly;
    p.isBasic      = isBasic;
    p.basicName    = basicName;
    p.cyclicKey    = cyclicKey;
    s_popups.push_back(std::move(p));

    if ((int)s_popups.size() > kMaxVisiblePopups)
        s_popups.erase(s_popups.begin());
}

//********************************************************************************
// NotifyState
//--------------------------------------------------------------------------------
// wasActive             occurrence's active state as of last frame; drives
//                       the active/inactive-edge transitions below
// leadFired             whether the "starting soon" popup already fired
//                       for the current upcoming occurrence
// lastSeenGeneration    bumped to s_notifyGeneration each frame this key
//                       is still a candidate; stale entries get pruned
//--------------------------------------------------------------------------------
// One entry per subscribed occurrence, keyed the same way LineSegment::key is
// built in subscriptions_bar.cpp, so state stays stable across frames regardless
// of row order. The active-edge (false -> true) fires the "now active" popup; the
// inactive-edge (true -> false) re-arms leadFired for the NEXT occurrence of a
// repeating slot/varying event. lastSeenGeneration replaces an O(n^2) rebuild-
// and-linear-search prune with an O(n) integer comparison. Edge case: a
// subscription already active on its very first frame reads as a false "just
// started" edge and fires an on-start popup for something that may have been
// running a while.
//--------------------------------------------------------------------------------
struct NotifyState
{
    bool wasActive = false;
    bool leadFired = false;
    uint64_t lastSeenGeneration = 0;   //. see O(n) prune note above
};

static std::unordered_map<std::string, NotifyState> s_notifyStates;
static uint64_t s_notifyGeneration = 0;   //. frame counter, see NotifyState::lastSeenGeneration

//********************************************************************************
// Candidate
//--------------------------------------------------------------------------------
// key, name, chatCode         identity + display/paste text
// active, secsUntilStart      timing; secsUntilStart meaningful only when
//                             !active, 0 when active
// isWeekly                   cosmetic only - drives the popup's red border
// toastEnabled, soundEnabled per-event notify-level opt-ins (see below)
// isBasic, basicName, cyclicKey   identity for the right-click "Mark done
//                             for today" menu, carried into the spawned Popup
//--------------------------------------------------------------------------------
// One notifiable Basic Event or Cyclic slot's current timing, collected fresh
// every frame - mirrors the row/segment-building loops in
// subscriptions_window.cpp/subscriptions_bar.cpp, over the same two sources:
// manually subscribed entries (always included), and auto-tracked active-and-
// incomplete weekly Wizard's Vault targets (weekly_vault.h) not already manually
// subscribed, included only while WeeklyAutoTrackEnabled is true.
// toastEnabled/soundEnabled (subscriptions.h's "notify level" ladder) only apply
// to manually subscribed items; a weekly-auto-track-only candidate isn't in
// either opt-in list and defaults to unconditional toast, no sound.
//--------------------------------------------------------------------------------
struct Candidate
{
    std::string key;
    std::string name;
    std::string chatCode;
    bool        active;
    int         secsUntilStart;
    bool        isWeekly;

    bool        toastEnabled = true;    //. per-event toast opt-in, see above
    bool        soundEnabled = false;   //. per-event sound opt-in, see above

    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CollectCandidates   (pairs with: UpdateNotifyStates)
//--------------------------------------------------------------------------------
// Builds one Candidate per resolved subscription (subscriptions_cache.h) with
// usable timing data. All "what's subscribed/auto-tracked, is it done today, is
// it a weekly target" derivation lives in subscriptions_cache.cpp, shared with
// subscriptions_window.cpp/ subscriptions_bar.cpp - this just adapts the shared,
// resolved list into this file's own Candidate shape.
//--------------------------------------------------------------------------------
static void CollectCandidates(std::vector<Candidate>& out, time_t now)
{
    //_ Shared cache; RefreshSubscriptionsCache is already called from RenderSubscriptionsNotifications below.
    const auto& resolved = GetResolvedSubscriptions();
    out.reserve(resolved.size());

    for (const auto& sub : resolved)
    {
        //_ Same "already done today" skip as the watchlist window/bar.
        if (sub.doneToday) continue;

        SubscriptionActiveState as = GetSubscriptionActiveState(sub, now);
        if (!as.active && as.secsUntilStart < 0) continue; //. no timer data yet

        bool toastEnabled = true;
        bool soundEnabled = false;
        if (sub.manuallySubscribed)
        {
            CyclicSubscriptionKey key{ sub.cyclicGroupName, sub.cyclicSlotOffset };
            toastEnabled = sub.isBasic
                ? IsBasicEventToastEnabled(sub.basicName)
                : IsCyclicSlotToastEnabled(key);
            soundEnabled = sub.isBasic
                ? IsBasicEventSoundEnabled(sub.basicName)
                : IsCyclicSlotSoundEnabled(key);
        }

        out.push_back({ sub.key, sub.label, sub.chatCode, as.active, as.secsUntilStart, sub.isWeeklyTarget,
                         toastEnabled, soundEnabled,
                         sub.isBasic, sub.basicName, CyclicSubscriptionKey{ sub.cyclicGroupName, sub.cyclicSlotOffset } });
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// UpdateNotifyStates   (pairs with: CollectCandidates)
//--------------------------------------------------------------------------------
// Walks this frame's candidates against s_notifyStates, firing popups on the
// edges described in NotifyState's comment above. Also prunes s_notifyStates of
// any key not seen this frame (e.g. the subscription was removed, or the
// underlying event/slot was deleted), so the map doesn't grow forever across a
// long play session as subscriptions come and go.
//--------------------------------------------------------------------------------
static void UpdateNotifyStates(const std::vector<Candidate>& candidates)
{
    //_ This frame's "still seen" marker, see NotifyState::lastSeenGeneration.
    s_notifyGeneration++;

    for (const auto& c : candidates)
    {
        NotifyState& st = s_notifyStates[c.key]; //. default-constructed on first sight
        st.lastSeenGeneration = s_notifyGeneration;

        if (c.active && !st.wasActive)
        {
            //_ Just went active this frame.
            if (NotificationOnStart && c.toastEnabled)
            {
                SpawnPopup(c.key, c.name, c.chatCode, "Now active!", ToImVec4(SubscriptionsActiveColor), c.isWeekly,
                           c.isBasic, c.basicName, c.cyclicKey);
                if (c.soundEnabled)
                    PlayNotificationSound(NotificationSoundFile);
            }

            //_ Already live, so the "starting soon" warning can no longer fire meaningfully - latch it regardless of toastEnabled.
            st.leadFired = true;
        }
        else if (!c.active && st.wasActive)
        {
            //_ Just ended - re-arm the lead popup for this slot/event's NEXT occurrence.
            st.leadFired = false;
        }

        if (!c.active && !st.leadFired && NotificationLeadMinutes > 0)
        {
            int leadSecs = NotificationLeadMinutes * 60;
            if (c.secsUntilStart >= 0 && c.secsUntilStart <= leadSecs)
            {
                if (c.toastEnabled)
                {
                    char buf[48];
                    snprintf(buf, sizeof(buf), "Starting in %dm %02ds", c.secsUntilStart / 60, c.secsUntilStart % 60);
                    SpawnPopup(c.key, c.name, c.chatCode, buf, ToImVec4(SubscriptionsSoonColor), c.isWeekly,
                               c.isBasic, c.basicName, c.cyclicKey);
                    if (c.soundEnabled)
                        PlayNotificationSound(NotificationSoundFile);
                }
                //_ Latches regardless of toastEnabled - otherwise enabling toast mid-window would retroactively fire this popup.
                st.leadFired = true;
            }
        }

        st.wasActive = c.active;
    }

    //_ Prune anything not seen this frame - O(n) integer comparisons, no string work (see lastSeenGeneration's comment above).
    for (auto it = s_notifyStates.begin(); it != s_notifyStates.end(); )
    {
        if (it->second.lastSeenGeneration != s_notifyGeneration)
            it = s_notifyStates.erase(it);
        else
            ++it;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawAndExpirePopups
//--------------------------------------------------------------------------------
// Draws every entry in s_popups stacked in the lower-right corner (newest closest
// to the corner), ages/fades them out, handles hover-pause and click-to-paste-
// and-dismiss, and removes anything that has finished fading.
//
// Uses the background draw list (same choice as RenderCyclicGroups in
// cyclicrender.cpp), so regular ImGui content - the right-click "Mark done"
// popup, the isWeekly hover tooltip - composites on top of the toast instead of
// under it; the toast still sits above the game world either way. Each popup's
// hit-test window is keyed by its stable p.key instead of loop index, since the
// index shifts whenever an earlier popup in the stack is erased.
//--------------------------------------------------------------------------------
static void DrawAndExpirePopups()
{
    if (s_popups.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;

    unsigned long long nowMs = GetTickCount64();
    unsigned long long displayMs = (unsigned long long)std::max(0, NotificationDisplaySeconds) * 1000ull;

    ImDrawList* dl = ImGui::GetBackgroundDrawList(); //. see header above

    std::vector<int> toRemove;

    //_ Newest is the LAST element in s_popups; stack closest to the corner so a fresh popup doesn't jump above already-open ones.
    for (int i = (int)s_popups.size() - 1; i >= 0; i--)
    {
        Popup& p = s_popups[i];
        int slotFromBottom = (int)s_popups.size() - 1 - i;

        float x = screenW - kMarginX - kPopupWidth;
        float y = screenH - kMarginY - kPopupHeight - (float)slotFromBottom * (kPopupHeight + kPopupGapY);

        //_ Same invisible-window hit-test subscriptions_bar.cpp uses - keyed by p.key, not loop index, see header above.
        std::string winId = "##we_notif_" + p.key;

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(kPopupWidth, kPopupHeight));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(winId.c_str(), nullptr,
            ImGuiWindowFlags_NoTitleBar         |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_NoMove             |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoSavedSettings    |
            ImGuiWindowFlags_NoBackground       |
            ImGuiWindowFlags_NoFocusOnAppearing |
            //_ FIXME: NoNav added to hopefully prevent a rare ImGui bug from happening. Remove when vendored ImGui version has been updated. (Imgui.cpp:7225)
            ImGuiWindowFlags_NoNav);
        ImGui::InvisibleButton("##we_notif_hit", ImVec2(kPopupWidth, kPopupHeight));
        bool hovered      = ImGui::IsItemHovered();
        bool clicked      = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::End();

        //_ Marks done for today, same as the window/bar's own menu. Doesn't dismiss the toast - the item vanishing elsewhere is confirmation enough.
        if (rightClicked)
        {
            ImGui::OpenPopup(("##we_notif_done_popup_" + p.key).c_str());
            io.WantCaptureMouse = true;
        }
        if (ImGui::BeginPopup(("##we_notif_done_popup_" + p.key).c_str()))
        {
            if (ImGui::Selectable("Mark done for today"))
            {
                if (p.isBasic) ToggleBasicEventDoneToday(p.basicName);
                else           ToggleCyclicSlotDoneToday(p.cyclicKey);
            }
            ImGui::Separator();
            if (ImGui::Selectable("Edit Subscriptions"))
                OpenEditSubscriptionsWindow(p.isBasic, p.basicName, p.cyclicKey);
            ImGui::EndPopup();
        }

        if (hovered)
        {
            //_ Nudges spawnedAtMs forward by this frame's delta while hovered, so elapsed time effectively stands still.
            unsigned long long frameMs = (unsigned long long)std::max(0.0f, io.DeltaTime * 1000.0f);
            p.spawnedAtMs += frameMs;
            io.WantCaptureMouse = true;

            //_ Same red-border meaning as the window's red dot tooltip.
            if (p.isWeekly)
                ImGui::SetTooltip("Counts toward this week's Wizard's Vault objectives.");
        }

        //_ Clicking dismisses the popup immediately, same as acting on a watchlist row; skips drawing/fading it since it's already being removed this frame.
        if (clicked)
        {
            std::string toCopy = BuildChatPasteMessage(p.name, p.chatCode);
            PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
            toRemove.push_back(i);
            io.WantCaptureMouse = true;
            continue;
        }

        //_ Signed and clamped at 0: an unsigned nowMs - spawnedAtMs could wrap huge if the hover-pause above nudged spawnedAtMs past nowMs.
        long long elapsedSigned = (long long)nowMs - (long long)p.spawnedAtMs;
        unsigned long long elapsed = elapsedSigned > 0 ? (unsigned long long)elapsedSigned : 0ull;
        unsigned long long totalMs = displayMs + kFadeOutMs;
        if (elapsed >= totalMs)
        {
            toRemove.push_back(i);
            continue;
        }

        float alpha = 1.0f;
        if (elapsed < kFadeInMs)
            alpha = (float)elapsed / (float)kFadeInMs;
        else if (elapsed > displayMs)
            alpha = 1.0f - (float)(elapsed - displayMs) / (float)kFadeOutMs;

        ImVec2 rectMin(x, y);
        ImVec2 rectMax(x + kPopupWidth, y + kPopupHeight);

        ImU32 bgCol     = ThemeColorU32(ImGuiCol_WindowBg, alpha);
        ImU32 accentCol = FadeU32(p.color, alpha);
        ImU32 textCol   = ThemeColorU32(ImGuiCol_Text, alpha);

        dl->AddRectFilled(rectMin, rectMax, bgCol, 6.0f);
        //_ Left-edge accent tinted per popup's status color, same as the window/bar use for the same states.
        dl->AddRectFilled(rectMin, ImVec2(rectMin.x + kAccentWidth, rectMax.y), accentCol, 6.0f);

        if (p.isWeekly)
        {
            //_ Weekly Vault marker, same meaning as the window's red dot; inset by half-thickness so the stroke isn't clipped by the corners.
            static constexpr float kWeeklyBorderThickness = 1.5f;
            ImU32 weeklyBorderCol = FadeU32(ToImVec4(WeeklyAutoTrackColor), alpha);
            dl->AddRect(
                ImVec2(rectMin.x + kWeeklyBorderThickness * 0.5f, rectMin.y + kWeeklyBorderThickness * 0.5f),
                ImVec2(rectMax.x - kWeeklyBorderThickness * 0.5f, rectMax.y - kWeeklyBorderThickness * 0.5f),
                weeklyBorderCol, 6.0f, 0, kWeeklyBorderThickness);
        }

        ImVec2 namePos(x + kAccentWidth + 10.0f, y + 8.0f);
        ImVec2 msgPos (x + kAccentWidth + 10.0f, y + 8.0f + ImGui::GetTextLineHeight() + 4.0f);
        dl->AddText(namePos, textCol, p.name.c_str());
        dl->AddText(msgPos,  accentCol, p.message.c_str());
    }

    if (!toRemove.empty())
    {
        std::sort(toRemove.begin(), toRemove.end());
        for (int i = (int)toRemove.size() - 1; i >= 0; i--)
            s_popups.erase(s_popups.begin() + toRemove[i]);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderSubscriptionsNotifications
//--------------------------------------------------------------------------------
// Collects candidates, updates the notify state machine, and draws/ages the popup
// stack. If NotificationsEnabled is false, returns immediately without clearing
// s_notifyStates/s_popups - a user toggling the feature off and back on mid-
// session shouldn't lose already-fired-this-occurrence bookkeeping, and any popup
// already on screen simply stops being drawn/aged until re-enabled, matching
// ShowSubscriptionsWindow/Bar's own behavior of leaving their underlying data
// untouched while hidden.
//--------------------------------------------------------------------------------
void RenderSubscriptionsNotifications()
{
    if (!NotificationsEnabled)
    {
        return;
    }

    time_t now = time(nullptr);
    {
        //_ Scoped to data gathering only, split from the draw timer below - see SubsBarDataTimer's equivalent split in subscriptions_bar.cpp.
        SubsNotifyDataTimer dataTimer; //. no-op unless ShowDebug
        RefreshSubscriptionsCache(now); //. no-op most frames

        std::vector<Candidate> candidates;
        CollectCandidates(candidates, now);
        UpdateNotifyStates(candidates);
    }

    //_ Popup draw/fade/expire - see g_AvgSubsNotifyDrawMs's comment in addon.h.
    SubsNotifyDrawTimer drawTimer; //. no-op unless ShowDebug
    DrawAndExpirePopups();
}