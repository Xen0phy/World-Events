// subscriptions_notification.cpp
// Toast-style popup notifications for subscribed Basic Events / Cyclic
// slots, PLUS auto-tracked active-and-incomplete weekly Wizard's Vault
// targets (weekly_vault.h) not already manually subscribed. A fourth view
// of the same subscription data as subscriptions_window.cpp /
// subscriptions_bar.cpp — see subscriptions.h.
//
// Two independent popups per notifiable occurrence:
//   - "starting soon", NotificationLeadMinutes before it starts
//   - "now active", the instant it actually starts
// both gated behind the NotificationsEnabled master switch (settings_table.h)
// AND, for manually subscribed items, a per-event opt-in (see
// IsBasicEventToastEnabled/IsCyclicSlotToastEnabled in subscriptions.h) —
// most subscriptions default to NOT showing a toast at all, since someone
// with a big watchlist usually only wants a handful of it to actually
// interrupt them. A candidate present purely via weekly auto-track
// (not manually subscribed) is unaffected by that per-event flag and
// keeps the master-switch-only behavior this always had.
//
// A manually subscribed item may ALSO opt into a notification sound
// (level 3 of the same notify-level ladder — see
// IsBasicEventSoundEnabled/IsCyclicSlotSoundEnabled in subscriptions.h),
// played via PlayNotificationSound(NotificationSoundFile) (notify_sound.h)
// alongside whichever popup just fired for it. Sound always implies toast
// in that ladder, so anywhere toastEnabled gates a SpawnPopup call,
// soundEnabled is checked right alongside it — a candidate can play the
// sound only on a fire that actually happened, never on its own.
// The auto-tracked weekly pass is separately gated behind
// WeeklyAutoTrackEnabled (settings_table.h), the same master switch the
// window/bar's equivalent auto-track passes share; a popup for a weekly
// target additionally gets a thin red border, matching the watchlist
// window's red dot / distribution bar's red dot marker for the same thing.
//
// Popups stack in the lower-right corner, newest closest to the corner,
// each auto-dismissing after NotificationDisplaySeconds (plus a short fade),
// and pasting the same "<name>: <chatCode>" text via PasteToChat on click
// as a row in the watchlist window / a segment on the distribution bar.

#include "subscriptions.h"
#include "subscriptions_cache.h"
#include "events_tracking.h"
#include "notify_sound.h"
#include "settings.h"
#include "imgui.h"
#include "addon.h" // SubsNotifyDataTimer/SubsNotifyDrawTimer — see their comment in addon.h
#include <windows.h>
#include <ctime>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kPopupWidth   = 300.0f;
static constexpr float kPopupHeight  = 56.0f;
static constexpr float kPopupGapY    = 8.0f;   // vertical gap between stacked popups
static constexpr float kMarginX      = 20.0f;  // from the right screen edge
static constexpr float kMarginY      = 20.0f;  // from the bottom screen edge
static constexpr float kAccentWidth  = 4.0f;   // colored left-edge stripe width

// Popups older than NotificationDisplaySeconds start fading out over this
// many ms before actually being removed, rather than just vanishing.
static constexpr unsigned long long kFadeOutMs = 400;

// Quick fade-in so a freshly-spawned popup doesn't just hard-cut into view.
static constexpr unsigned long long kFadeInMs = 150;

// Hard cap on simultaneously-visible popups — if more fire before older
// ones have finished dismissing, the OLDEST is dropped immediately rather
// than letting the stack grow without bound (e.g. several cyclic slots in
// the same group crossing their lead threshold in the same frame).
static constexpr int kMaxVisiblePopups = 4;

// ---------------------------------------------------------------------------
// StatusColorToImU32
// ---------------------------------------------------------------------------
// Converts a packed RRGGBBAA value (SubscriptionsActiveColor/SoonColor's
// storage convention — see settings_table.h) into an ImU32 (ABGR) with the
// given alpha multiplier baked in. Deliberately a local, self-contained
// copy rather than a shared helper: subscriptions_window.cpp and
// subscriptions_bar.cpp each already keep their own equivalent conversion
// rather than factoring one out, since the three files' color needs (plain
// ImVec4 for TextColored, a drawlist ImU32, and this alpha-baked ImU32) all
// differ slightly.
// ---------------------------------------------------------------------------
static ImU32 StatusColorToImU32(unsigned int rgba, float alphaMul)
{
    unsigned char r = (unsigned char)((rgba >> 24) & 0xFF);
    unsigned char g = (unsigned char)((rgba >> 16) & 0xFF);
    unsigned char b = (unsigned char)((rgba >>  8) & 0xFF);
    unsigned char a = (unsigned char)(255.0f * (alphaMul < 0.0f ? 0.0f : (alphaMul > 1.0f ? 1.0f : alphaMul)));
    return IM_COL32(r, g, b, a);
}

// ---------------------------------------------------------------------------
// ThemeColorU32
// ---------------------------------------------------------------------------
// Reads whatever Nexus/the user currently has the shared ImGui context
// themed to (ImGuiCol_WindowBg/ImGuiCol_Text — same context AddonLoad hands
// off via ImGui::SetCurrentContext, see addon.cpp) instead of a color this
// addon picks itself, so the toast's background/name text match every
// other Nexus window rather than a hardcoded dark gray/white. alphaMul
// (0..1) multiplies the style color's OWN alpha rather than replacing it,
// so a translucent theme keeps its translucency, just faded further
// in/out on top of it by this popup's own fade animation.
// ---------------------------------------------------------------------------
static ImU32 ThemeColorU32(ImGuiCol styleColor, float alphaMul)
{
    ImVec4 c = ImGui::GetStyleColorVec4(styleColor);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul));
}

// ---------------------------------------------------------------------------
// Popup
// ---------------------------------------------------------------------------
// One spawned toast. `spawnedAtMs` is a GetTickCount64() timestamp — same
// wall-clock source subscriptions_window.cpp's click-flash already uses —
// and doubles as a "pause" mechanism: while the mouse hovers a popup, this
// file nudges spawnedAtMs forward frame-by-frame instead of letting it age,
// so a popup the user is actively reading/about to click can't expire out
// from under the cursor.
// ---------------------------------------------------------------------------
struct Popup
{
    std::string key;      // same stable identity as NotifyState's map key, only used for a stable ImGui window id
    std::string name;      // display name, e.g. "Tequatl the Sunless" or "Domain of Vabbi - Forged Assault"
    std::string chatCode;
    std::string message;   // e.g. "Starting in 4m 32s" or "Now active!"
    unsigned int color;    // packed RRGGBBAA — SubscriptionsSoonColor for the lead popup, SubscriptionsActiveColor for the start popup
    unsigned long long spawnedAtMs;
    bool isWeekly = false; // active-and-incomplete weekly Wizard's Vault target this week (weekly_vault.h) — draws an
                           // additional thin red border, same "counts toward this week's Wizard's Vault objective"
                           // marker the watchlist window's red dot / distribution bar's red dot convey elsewhere;
                           // purely visual, doesn't affect click/dismiss behavior below.

    // Identity for the right-click "Mark done for today" menu — see
    // DrawAndExpirePopups below. Mirrors Candidate's own copy of the same
    // trio (isBasic/basicName/cyclicKey), which is where these get filled
    // in via SpawnPopup's extra parameters.
    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

static std::vector<Popup> s_popups;

static void SpawnPopup(const std::string& key, const std::string& name, const std::string& chatCode,
                        const std::string& message, unsigned int rgba, bool isWeekly,
                        bool isBasic, const std::string& basicName, const CyclicSubscriptionKey& cyclicKey)
{
    Popup p;
    p.key          = key;
    p.name         = name;
    p.chatCode     = chatCode;
    p.message      = message;
    p.color        = rgba;
    p.spawnedAtMs  = GetTickCount64();
    p.isWeekly     = isWeekly;
    p.isBasic      = isBasic;
    p.basicName    = basicName;
    p.cyclicKey    = cyclicKey;
    s_popups.push_back(std::move(p));

    // Drop the OLDEST rather than refusing the new one — the new arrival is
    // by definition the most time-sensitive popup right now.
    if ((int)s_popups.size() > kMaxVisiblePopups)
        s_popups.erase(s_popups.begin());
}

// ---------------------------------------------------------------------------
// NotifyState / s_notifyStates
// ---------------------------------------------------------------------------
// One entry per subscribed occurrence, keyed the same way LineSegment::key
// is built in subscriptions_bar.cpp ("Basic:<name>" / "Cyclic:<group>:
// <offset>"), so an occurrence's notification state stays stable across
// frames regardless of row order.
//
// wasActive: this occurrence's active/inactive state as of last frame —
// the active-edge (false -> true) is what fires the "now active" popup,
// and the inactive-edge (true -> false) is what re-arms leadFired for the
// NEXT occurrence of a repeating slot/varying event.
//
// leadFired: whether the "starting soon" popup has already fired for the
// CURRENT upcoming occurrence. Set on fire, cleared when the occurrence
// ends (see above) or the instant it goes active (so a lead popup can
// never fire retroactively for an occurrence that has already started).
//
// NOTE: a subscription's very first frame (e.g. the addon just loaded, or
// the user just subscribed) always starts from wasActive=false/
// leadFired=false. If that occurrence happens to ALREADY be active on that
// first frame, this reads as a false "just started" edge and fires an
// on-start popup for something that may have been running for a while —
// an accepted, minor edge case rather than something worth a separate
// "first frame seen" flag for.
// ---------------------------------------------------------------------------
struct NotifyState
{
    bool wasActive = false;
    bool leadFired = false;

    // Bumped to s_notifyGeneration every frame this key appears in
    // CollectCandidates' output. Anything left at a stale (older)
    // generation after a pass is exactly the set that's no longer
    // subscribed/tracked and gets pruned — see UpdateNotifyStates below.
    // Replaces an earlier approach that rebuilt a std::vector<std::string>
    // of every seen key each frame and did a linear std::find through it
    // per s_notifyStates entry to prune — an O(n^2) string-comparison scan
    // that ran in full every frame regardless of whether anything was
    // actually being drawn, since s_notifyStates only grows over a play
    // session (every distinct event/slot ever subscribed or auto-tracked).
    // A single pass bumping this counter, then a single pass comparing
    // integers, is O(n) with no string comparisons involved in the prune
    // at all.
    uint64_t lastSeenGeneration = 0;
};

static std::unordered_map<std::string, NotifyState> s_notifyStates;
static uint64_t s_notifyGeneration = 0;

// ---------------------------------------------------------------------------
// Candidate
// ---------------------------------------------------------------------------
// One notifiable Basic Event or Cyclic slot's current timing, collected
// fresh every frame — mirrors the row/segment-building loops in
// subscriptions_window.cpp/subscriptions_bar.cpp, and now over the same two
// sources those two views cover:
//   - manually subscribed entries (g_SubscribedBasicEvents/
//     g_SubscribedCyclicSlots), always included, and
//   - auto-tracked active-and-incomplete weekly Wizard's Vault targets
//     (weekly_vault.h) NOT already manually subscribed, included only while
//     WeeklyAutoTrackEnabled (settings_table.h) is true — the same master
//     switch the window/bar auto-track passes are gated behind.
// isWeekly is purely cosmetic here: it drives the thin red border
// DrawAndExpirePopups paints around a popup for the same "counts toward
// this week's Wizard's Vault objective" reason the window's red dot /
// bar's red dot marker exist, and has no bearing on whether/when a popup
// fires.
// ---------------------------------------------------------------------------
struct Candidate
{
    std::string key;
    std::string name;
    std::string chatCode;
    bool        active;
    int         secsUntilStart; // meaningful only when !active; 0 when active
    bool        isWeekly;

    // Per-event opt-in for this candidate actually popping a toast at
    // all — see subscriptions.h's "notify level" comment. Only meaningful
    // for manually subscribed items; a candidate present purely via
    // weekly auto-track (manuallySubscribed == false) isn't in either
    // toast list at all, so it keeps its previous unconditional behavior
    // (gated only by WeeklyAutoTrackEnabled elsewhere) rather than
    // silently going quiet because it's absent from a list it was never
    // meant to be checked against.
    bool        toastEnabled = true;

    // Same idea, one level up the ladder: per-event opt-in for playing
    // the configured notification sound alongside a fired popup. Only
    // meaningful for manually subscribed items, same reasoning as
    // toastEnabled above — a weekly-auto-track-only candidate defaults to
    // false here (no sound), matching its long-standing toast-only
    // unconditional behavior rather than gaining a new sound side effect
    // it never opted into.
    bool        soundEnabled = false;

    // Identity for the right-click "Mark done for today" menu — carried
    // through into the spawned Popup via SpawnPopup. See Popup's own copy
    // of this trio above.
    bool        isBasic = true;
    std::string basicName;
    CyclicSubscriptionKey cyclicKey;
};

// Builds one Candidate per resolved subscription (see subscriptions_cache.h)
// with usable timing data. All the "what's subscribed / auto-tracked, is it
// done today, is it a weekly target" derivation now lives in
// subscriptions_cache.cpp, shared with subscriptions_window.cpp/
// subscriptions_bar.cpp — this just adapts the shared, resolved list into
// this file's own Candidate shape.
static void CollectCandidates(std::vector<Candidate>& out, time_t now)
{
    const auto& resolved = GetResolvedSubscriptions(); // shared cache — RefreshSubscriptionsCache already called from RenderSubscriptionsNotifications below
    out.reserve(resolved.size());

    for (const auto& sub : resolved)
    {
        if (sub.doneToday) continue; // same "already done today" skip as the watchlist window/bar

        SubscriptionActiveState as = GetSubscriptionActiveState(sub, now);
        if (!as.active && as.secsUntilStart < 0) continue; // no timer data yet

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

// ---------------------------------------------------------------------------
// UpdateNotifyStates
// ---------------------------------------------------------------------------
// Walks this frame's candidates against s_notifyStates, firing popups on
// the edges described in NotifyState's comment above. Also prunes
// s_notifyStates of any key NOT seen this frame (e.g. the user removed
// that subscription, or the underlying event/slot was deleted), so the map
// doesn't grow forever across a long play session as subscriptions come
// and go.
// ---------------------------------------------------------------------------
static void UpdateNotifyStates(const std::vector<Candidate>& candidates)
{
    s_notifyGeneration++; // this frame's "still seen" marker — see NotifyState::lastSeenGeneration

    for (const auto& c : candidates)
    {
        NotifyState& st = s_notifyStates[c.key]; // default-constructed (false/false/0) on first sight
        st.lastSeenGeneration = s_notifyGeneration;

        if (c.active && !st.wasActive)
        {
            // Just went active this frame.
            if (NotificationOnStart && c.toastEnabled)
            {
                SpawnPopup(c.key, c.name, c.chatCode, "Now active!", SubscriptionsActiveColor, c.isWeekly,
                           c.isBasic, c.basicName, c.cyclicKey);
                if (c.soundEnabled)
                    PlayNotificationSound(NotificationSoundFile);
            }

            // An occurrence that's already live can no longer meaningfully
            // fire its "starting soon" warning — mark it fired so a lead
            // popup can't pop up retroactively for something already
            // underway. Marked regardless of toastEnabled, same reasoning
            // as below: the state machine tracks reality; toastEnabled
            // only gates whether a popup actually shows for it.
            st.leadFired = true;
        }
        else if (!c.active && st.wasActive)
        {
            // Just ended — re-arm the lead popup for this slot/event's
            // NEXT occurrence.
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
                    SpawnPopup(c.key, c.name, c.chatCode, buf, SubscriptionsSoonColor, c.isWeekly,
                               c.isBasic, c.basicName, c.cyclicKey);
                    if (c.soundEnabled)
                        PlayNotificationSound(NotificationSoundFile);
                }
                // leadFired latches regardless of toastEnabled — otherwise
                // toggling toast on mid-window (between the lead threshold
                // and actual start) would fire a "starting in Xm" popup
                // retroactively, the instant it's enabled, for a window
                // that's already most of the way elapsed.
                st.leadFired = true;
            }
        }

        st.wasActive = c.active;
    }

    // Prune anything not subscribed/found this frame — O(n) integer
    // comparisons, no string work at all (contrast the old seenKeys +
    // std::find approach this replaced, see NotifyState's comment above).
    for (auto it = s_notifyStates.begin(); it != s_notifyStates.end(); )
    {
        if (it->second.lastSeenGeneration != s_notifyGeneration)
            it = s_notifyStates.erase(it);
        else
            ++it;
    }
}

// ---------------------------------------------------------------------------
// DrawAndExpirePopups
// ---------------------------------------------------------------------------
// Draws every entry in s_popups stacked in the lower-right corner (newest
// closest to the corner), ages/fades them out, handles hover-pause and
// click-to-paste-and-dismiss, and removes anything that has finished
// fading. Drawn on the foreground draw list (on top of everything else,
// including the game world and other ImGui windows) since a notification
// that could be covered by an already-open window would defeat the point.
// ---------------------------------------------------------------------------
static void DrawAndExpirePopups()
{
    if (s_popups.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;

    unsigned long long nowMs = GetTickCount64();
    unsigned long long displayMs = (unsigned long long)std::max(0, NotificationDisplaySeconds) * 1000ull;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    std::vector<int> toRemove;

    // Newest is the LAST element in s_popups; stack it closest to the
    // corner and older ones progressively further up, so a freshly-fired
    // popup doesn't visually jump above already-open ones.
    for (int i = (int)s_popups.size() - 1; i >= 0; i--)
    {
        Popup& p = s_popups[i];
        int slotFromBottom = (int)s_popups.size() - 1 - i;

        float x = screenW - kMarginX - kPopupWidth;
        float y = screenH - kMarginY - kPopupHeight - (float)slotFromBottom * (kPopupHeight + kPopupGapY);

        // ---- Click / hover hit-test ----
        // Same "invisible window + InvisibleButton" pattern
        // subscriptions_bar.cpp uses for its own click detection — a plain
        // IsMouseClicked()/IsMouseHoveringRect() check doesn't reliably
        // route through other ImGui windows/game input correctly, an
        // actual (fully transparent) window does.
        // Keyed by p.key (the same stable identity used in s_notifyStates),
        // not the loop index — the index shifts every time an earlier
        // popup in the stack is erased, which would otherwise hand a
        // different occurrence's popup a previously-used ImGui ID mid-fade.
        std::string winId = "##we_notif_" + p.key;

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(kPopupWidth, kPopupHeight));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(winId.c_str(), nullptr,
            ImGuiWindowFlags_NoTitleBar        |
            ImGuiWindowFlags_NoResize          |
            ImGuiWindowFlags_NoMove            |
            ImGuiWindowFlags_NoScrollbar       |
            ImGuiWindowFlags_NoSavedSettings   |
            ImGuiWindowFlags_NoBackground      |
            ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::InvisibleButton("##we_notif_hit", ImVec2(kPopupWidth, kPopupHeight));
        bool hovered      = ImGui::IsItemHovered();
        bool clicked      = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::End();

        // Right-click: mark this event/slot done for today — same
        // Toggle*DoneToday calls as the watchlist window/bar's own
        // right-click menus (seeevents_tracking.h). Doesn't dismiss the
        // toast itself; the occurrence disappearing from the other views
        // is confirmation enough, and dismissing here too would make a
        // misclick harder to undo before reaching for the options panel's
        // "Clear all manual done markers" button.
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
            ImGui::EndPopup();
        }

        if (hovered)
        {
            // Pause aging while the mouse sits over this popup — nudge
            // spawnedAtMs forward by this frame's delta so elapsed time
            // effectively stands still, rather than letting a popup the
            // user is reading/about to click expire out from under them.
            unsigned long long frameMs = (unsigned long long)std::max(0.0f, io.DeltaTime * 1000.0f);
            p.spawnedAtMs += frameMs;
            io.WantCaptureMouse = true;

            // Same red-border explanation as the watchlist window's red dot
            // tooltip, surfaced here too since the border alone is a subtler
            // marker than an explicit dot.
            if (p.isWeekly)
                ImGui::SetTooltip("Counts toward this week's Wizard's Vault objectives.");
        }

        if (clicked)
        {
            std::string toCopy = BuildChatPasteMessage(p.name, p.chatCode);
            PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));
            toRemove.push_back(i); // clicking dismisses it immediately, same as acting on a watchlist row
            io.WantCaptureMouse = true;
            continue; // don't bother drawing/fading a popup that's being removed this frame
        }

        // ---- Age / fade ----
        // Signed subtraction, clamped at 0: the hover-pause above nudges
        // spawnedAtMs forward using io.DeltaTime, a per-frame estimate that
        // can drift slightly ahead of GetTickCount64()'s own clock after
        // several consecutive hovered frames. An unsigned nowMs - spawnedAtMs
        // would silently wrap to a huge value in that case (spawnedAtMs
        // momentarily > nowMs) and instantly expire the popup being hovered
        // — exactly what the pause is supposed to prevent.
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

        // ---- Draw ----
        ImVec2 rectMin(x, y);
        ImVec2 rectMax(x + kPopupWidth, y + kPopupHeight);

        ImU32 bgCol     = ThemeColorU32(ImGuiCol_WindowBg, alpha);
        ImU32 accentCol = StatusColorToImU32(p.color, alpha);
        ImU32 textCol   = ThemeColorU32(ImGuiCol_Text, alpha);

        dl->AddRectFilled(rectMin, rectMax, bgCol, 6.0f);
        // Colored left-edge accent stripe, tinted per popup's status color
        // (SubscriptionsSoonColor for the lead popup, SubscriptionsActiveColor
        // for the on-start popup) — same colors the watchlist window/bar
        // already use for the equivalent states, so the popup reads as
        // "the same thing, just surfaced differently" rather than
        // introducing a third color scheme.
        dl->AddRectFilled(rectMin, ImVec2(rectMin.x + kAccentWidth, rectMax.y), accentCol, 6.0f);

        // Thin red outline marking this as an active-and-incomplete weekly
        // Wizard's Vault target this week — the notification popup's
        // equivalent of the watchlist window's red dot / distribution
        // bar's red dot marker (see weekly_vault.h/.cpp for what sets
        // isWeekly). Drawn last so it sits on top of the fill/stripe, and
        // inset by half its own thickness so AddRect's stroke doesn't get
        // clipped by the popup's own rounded corners.
        if (p.isWeekly)
        {
            static constexpr float kWeeklyBorderThickness = 1.5f;
            ImU32 weeklyBorderCol = IM_COL32(219, 40, 40, (int)(255 * alpha));
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

// ---------------------------------------------------------------------------
// RenderSubscriptionsNotifications
// ---------------------------------------------------------------------------
void RenderSubscriptionsNotifications()
{
    if (!NotificationsEnabled)
    {
        // Deliberately not clearing s_notifyStates/s_popups here: a user
        // toggling the feature off and back on mid-session shouldn't lose
        // already-fired-this-occurrence bookkeeping, and any popup already
        // on screen when it's toggled off simply stops being drawn/aged
        // until re-enabled (matching ShowSubscriptionsWindow/Bar, which
        // likewise leave their underlying data untouched while hidden).
        return;
    }

    time_t now = time(nullptr);
    {
        // Scoped to just data gathering/resolution — see SubsBarDataTimer's
        // equivalent comment in subscriptions_bar.cpp for why this is split
        // from SubsNotifyDrawTimer below.
        SubsNotifyDataTimer dataTimer; // no-op unless ShowDebug — see addon.h
        RefreshSubscriptionsCache(now); // no-op most frames — see subscriptions_cache.h

        std::vector<Candidate> candidates;
        CollectCandidates(candidates, now);
        UpdateNotifyStates(candidates);
    }

    // Popup draw/fade/expire — see g_AvgSubsNotifyDrawMs's comment in addon.h.
    SubsNotifyDrawTimer drawTimer; // no-op unless ShowDebug — see addon.h
    DrawAndExpirePopups();
}
