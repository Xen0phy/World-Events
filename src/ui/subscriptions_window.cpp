// subscriptions_window.cpp
// Draws the standalone "Subscriptions" watchlist window.

#include "subscriptions.h"
#include "events.h"
#include "maprender.h"
#include "settings.h"
#include "gw2_api.h"
#include "weekly_vault.h"
#include "imgui.h"
#include <windows.h>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// GetCyclicSlotStatus
// ---------------------------------------------------------------------------
// Returns whether the given slot (identified by its offset within grp) is
// currently active, and either the seconds left (if active) or the
// seconds until its next occurrence starts (if not) — same phase math as
// the per-slot tooltip loop in cyclicrender.cpp's RenderCyclicGroups,
// factored out here since the watchlist window needs the identical
// "soonest occurrence across every repeat" reduction but has no arc to
// draw alongside it.
//
// Returns false via the `found` out-parameter if grp.slots no longer
// contains a slot at this offset (e.g. the user deleted it from the
// options panel since subscribing) — the caller skips drawing that row
// but leaves the dangling subscription in place rather than silently
// deleting it, matching how a deleted Basic Event's subscription is
// likewise left alone (see the row loop below for the same treatment).
// ---------------------------------------------------------------------------
struct SlotStatus { bool found; bool active; int secs; ImU32 color; };

static SlotStatus GetCyclicSlotStatus(const CyclicGroup& grp, int slotOffset, time_t now)
{
    for (const auto& slot : grp.slots)
    {
        if (slot.offset != slotOffset) continue;

        int secondsOfDay = (int)(now % grp.period);
        int repeat  = slot.repeat > 0 ? slot.repeat : 1;
        int subSpan = grp.period / repeat;

        bool foundActive    = false;
        int  activeSecsLeft = 0;
        int  bestSecsUntil  = grp.period;

        for (int r = 0; r < repeat; r++)
        {
            int baseOffset     = slot.offset + r * subSpan;
            int phase          = ((secondsOfDay - baseOffset) % grp.period + grp.period) % grp.period;
            bool active        = (phase < slot.duration);
            int secsUntilStart = active ? 0 : (grp.period - phase);

            if (active)
            {
                foundActive    = true;
                activeSecsLeft = slot.duration - phase;
                break; // a slot can't be active in two repeats at once
            }
            else if (secsUntilStart < bestSecsUntil)
            {
                bestSecsUntil = secsUntilStart;
            }
        }

        ImU32 color = grp.SlotColor(slot);
        return foundActive
            ? SlotStatus{ true, true,  activeSecsLeft, color }
            : SlotStatus{ true, false, bestSecsUntil,  color };
    }

    return SlotStatus{ false, false, 0, 0 };
}

// ---------------------------------------------------------------------------
// SubscriptionColorToImVec4
// ---------------------------------------------------------------------------
// SubscriptionsActiveColor/SoonColor are stored as packed RRGGBBAA (R in
// the top byte — same convention as ColorSet::base in cyclic.h and the
// map's BasicEventColor* settings), which is NOT the same byte order as
// ImGui's own ImU32 (ABGR). The low byte (alpha) is deliberately ignored
// here: these feed straight into ImGui::TextColored, which is plain text
// with a fixed opacity of 1.0, not a translucency-capable draw — see the
// long comment on these two settings in settings_table.h for why the
// options-panel picker for them is a ColorEdit3 (RGB only) rather than
// the ColorEdit4 the map's own status colors use.
// ---------------------------------------------------------------------------
static ImVec4 SubscriptionColorToImVec4(unsigned int rgba)
{
    return ImVec4(
        ((rgba >> 24) & 0xFF) / 255.0f, // R
        ((rgba >> 16) & 0xFF) / 255.0f, // G
        ((rgba >>  8) & 0xFF) / 255.0f, // B
        1.0f                             // alpha channel of `rgba` unused — see above
    );
}

// 15 minutes — matches the "soon" threshold already hardcoded for
// BasicEventColorSoon on the map (maprender.cpp's `secs < 900` check),
// reused here rather than adding a second configurable window per the
// call made this session.
static constexpr int kSoonThresholdSecs = 900;

// ---------------------------------------------------------------------------
// s_flashUntil / s_flashKey
// ---------------------------------------------------------------------------
// Tracks the single most-recently-clicked row (by its display name, which
// is unique within one frame's row list — two rows can't show the exact
// same "Group — Slot" or event name at once) and a GetTickCount64()
// deadline for how long to keep flashing it. Static/file-local: this
// window draws on the main thread only, once per frame, so plain statics
// are fine here — no cross-thread access, unlike PasteToChat's detached
// clipboard-then-paste thread elsewhere in the project.
// ---------------------------------------------------------------------------
static std::string   s_flashKey;
static unsigned long long s_flashUntil = 0;

static constexpr unsigned long long kFlashDurationMs = 350;

// ---------------------------------------------------------------------------
// DrawSubscriptionRow
// ---------------------------------------------------------------------------
// Draws one watchlist row as a clickable Selectable rather than plain
// Text, so the whole line acts like a button: click copies
// "<name>: <chatCode>" to the clipboard (or just "<name>" if no chat code
// is set for that event/slot yet — nothing to append, so nothing is
// appended, rather than leaving a dangling "Name: " with an empty tail).
//
// Selectable (not a manual InvisibleButton+Text pair) is used because it
// already gives free hover highlighting — a useful, standard affordance
// that the row is clickable — and handles the "whole line is one widget"
// sizing correctly without the FramePadding fights DrawSubscribeCheckbox
// had to work around for the options-panel checkboxes.
//
// Text color still follows the same three-state Active/Soon/default rule
// as before; a just-clicked row additionally flashes a bright highlight
// for kFlashDurationMs as click confirmation (see s_flashKey/s_flashUntil
// above) — chosen instead of a tooltip so the confirmation doesn't cover
// the very text the user just clicked, and instead of a persistent
// "Copied!" label so the window doesn't visually shift/grow when clicked.
// ---------------------------------------------------------------------------
static void DrawSubscriptionRow(const std::string& name, const std::string& chatCode, bool active, int secs, bool isWeekly)
{
    if (isWeekly)
    {
        // Small red dot marking this row as an active-and-incomplete
        // weekly Wizard's Vault target this week — see weekly_vault.h/
        // .cpp for what sets isWeekly and the event/slot -> objective
        // mapping table. Purely visual; doesn't affect the row's click
        // behavior below.
        ImGui::TextColored(ImVec4(0.86f, 0.16f, 0.16f, 1.0f), "\xE2\x97\x8F"); // U+25CF BLACK CIRCLE
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Counts toward this week's Wizard's Vault objectives.");
        ImGui::SameLine(0.0f, 4.0f);
    }

    std::string statusSuffix;
    ImVec4 color;
    bool useColor = true;

    if (active)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), " — Active (ends in %dm %02ds)", secs / 60, secs % 60);
        statusSuffix = buf;
        color = SubscriptionColorToImVec4(SubscriptionsActiveColor);
    }
    else if (secs < kSoonThresholdSecs)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), " — in %dm %02ds", secs / 60, secs % 60);
        statusSuffix = buf;
        color = SubscriptionColorToImVec4(SubscriptionsSoonColor);
    }
    else
    {
        char buf[32];
        if (secs >= 3600)
            snprintf(buf, sizeof(buf), " — in %dh %02dm", secs / 3600, (secs % 3600) / 60);
        else
            snprintf(buf, sizeof(buf), " — in %dm %02ds", secs / 60, secs % 60);
        statusSuffix = buf;
        useColor = false;
    }

    std::string label = name + statusSuffix;

    bool flashing = (s_flashKey == name) && (GetTickCount64() < s_flashUntil);

    int pushedColors = 0;
    if (flashing)
    {
        // Bright, attention-grabbing flash — deliberately NOT reusing
        // SubscriptionsActiveColor/SoonColor, so a click on an active/soon
        // row still visibly confirms even though those rows are already
        // colored; a fixed white flash reads as "just happened" regardless
        // of the row's own status color underneath.
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(1.0f, 1.0f, 1.0f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
        pushedColors = 3;
    }

    if (useColor) ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool clicked = ImGui::Selectable(label.c_str());
    if (useColor) ImGui::PopStyleColor();

    if (pushedColors) ImGui::PopStyleColor(pushedColors);

    if (clicked)
    {
        std::string toCopy = chatCode.empty() ? name : (name + ": " + chatCode);
        PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));

        s_flashKey   = name;
        s_flashUntil = GetTickCount64() + kFlashDurationMs;
    }
}

// ---------------------------------------------------------------------------
// RenderSubscriptionsWindow
// ---------------------------------------------------------------------------
void RenderSubscriptionsWindow()
{
    if (!ShowSubscriptionsWindow) return;

    time_t now = time(nullptr);

    // Sortable, unified list of rows across both Basic Events and Cyclic
    // slots so "what's coming up soonest" reads as one list rather than
    // two separate sections the user has to visually merge themselves —
    // active entries first, then soonest-upcoming, matching the sort
    // already used for the per-group tooltip in cyclicrender.cpp.
    struct Row { std::string name; std::string chatCode; bool active; int secs; bool isWeekly; };
    std::vector<Row> rows;
    rows.reserve(g_SubscribedBasicEvents.size() + g_SubscribedCyclicSlots.size());

    for (const auto& evName : g_SubscribedBasicEvents)
    {
        auto it = std::find_if(g_Events.begin(), g_Events.end(),
            [&](const WorldEvent& ev) { return ev.name == evName; });
        if (it == g_Events.end()) continue; // deleted since subscribing — skip, leave the subscription alone

        // Only meaningful for the 13 Core Bosses (see events.h's
        // apiWorldBossId) — empty for everything else, so this is a
        // no-op for the rest of the list regardless of API key/status.
        // Independent of the weekly Wizard's Vault check below — see
        // weekly_vault.h.
        if (!it->apiWorldBossId.empty() && IsWorldBossCompletedToday(it->apiWorldBossId))
            continue;

        bool active = IsEventActive(*it, now);
        int  secs   = active ? GetSecondsUntilEventEnd(*it, now) : GetSecondsUntilEventStart(*it, now);
        if (secs < 0) continue; // no timer data yet
        if (active && SubscriptionsHideActive) continue; // "only show what's not already happening"

        bool weeklyComplete = false;
        bool isWeekly = IsBasicEventWeeklyTarget(it->name, weeklyComplete) && !weeklyComplete;
        rows.push_back({ it->name, it->chatCode, active, secs, isWeekly });
    }

    // Auto-tracked: NOT manually subscribed, but an active-and-incomplete
    // weekly Wizard's Vault target this week — see weekly_vault.cpp for
    // where to adjust which events count. Disappears again on its own
    // the moment the objective completes; a manually-subscribed one
    // (handled by the loop just above) stays regardless.
    for (const auto& ev : g_Events)
    {
        bool alreadyManual = std::find(g_SubscribedBasicEvents.begin(), g_SubscribedBasicEvents.end(), ev.name) != g_SubscribedBasicEvents.end();
        if (alreadyManual) continue;

        bool weeklyComplete = false;
        if (!IsBasicEventWeeklyTarget(ev.name, weeklyComplete)) continue;
        if (weeklyComplete) continue;

        if (!ev.apiWorldBossId.empty() && IsWorldBossCompletedToday(ev.apiWorldBossId))
            continue;

        bool active = IsEventActive(ev, now);
        int  secs   = active ? GetSecondsUntilEventEnd(ev, now) : GetSecondsUntilEventStart(ev, now);
        if (secs < 0) continue;
        if (active && SubscriptionsHideActive) continue;

        rows.push_back({ ev.name, ev.chatCode, active, secs, true });
    }

    for (const auto& key : g_SubscribedCyclicSlots)
    {
        auto it = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
            [&](const CyclicGroup& grp) { return grp.name == key.groupName; });
        if (it == g_CyclicGroups.end()) continue; // group deleted since subscribing

        // Group-level equivalent of the apiWorldBossId check above — see
        // CyclicGroup::apiMapChestId in events.h for why this applies to
        // the WHOLE group rather than just this one slot. No-op for every
        // group except the 8 HoT/PoF maps /v2/account/mapchests covers.
        if (!it->apiMapChestId.empty() && IsMapChestClaimedToday(it->apiMapChestId))
            continue;

        SlotStatus status = GetCyclicSlotStatus(*it, key.slotOffset, now);
        if (!status.found) continue; // slot deleted/re-offset since subscribing
        if (status.active && SubscriptionsHideActive) continue;

        // Prefix with the group name so two groups' same-named slots
        // (e.g. Dry Top's two "Crash Site" occurrences) stay
        // distinguishable in the flat watchlist.
        for (const auto& slot : it->slots)
        {
            if (slot.offset != key.slotOffset) continue;
            bool weeklyComplete = false;
            bool isWeekly = IsCyclicSlotWeeklyTarget(it->name, slot.name, weeklyComplete) && !weeklyComplete;
            rows.push_back({ it->name + " - " + slot.name, slot.chatCode, status.active, status.secs, isWeekly });
            break;
        }
    }

    // Auto-tracked weekly targets not already manually subscribed — same
    // rule as the Basic Events pass above.
    for (const auto& grp : g_CyclicGroups)
    {
        if (!grp.apiMapChestId.empty() && IsMapChestClaimedToday(grp.apiMapChestId))
            continue;

        for (const auto& slot : grp.slots)
        {
            bool alreadyManual = std::find_if(g_SubscribedCyclicSlots.begin(), g_SubscribedCyclicSlots.end(),
                [&](const CyclicSubscriptionKey& k) { return k.groupName == grp.name && k.slotOffset == slot.offset; })
                != g_SubscribedCyclicSlots.end();
            if (alreadyManual) continue;

            bool weeklyComplete = false;
            if (!IsCyclicSlotWeeklyTarget(grp.name, slot.name, weeklyComplete)) continue;
            if (weeklyComplete) continue;

            SlotStatus status = GetCyclicSlotStatus(grp, slot.offset, now);
            if (!status.found) continue;
            if (status.active && SubscriptionsHideActive) continue;

            rows.push_back({ grp.name + " - " + slot.name, slot.chatCode, status.active, status.secs, true });
        }
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
    {
        if (a.active != b.active) return a.active; // active first
        return a.secs < b.secs;                    // then soonest first
    });

    ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Events — Subscriptions", &ShowSubscriptionsWindow))
    {
        // Collapsed (not closed) — still need End() to balance Begin().
        ImGui::End();
        return;
    }

    if (rows.empty())
    {
        bool hasSubscriptions = !(g_SubscribedBasicEvents.empty() && g_SubscribedCyclicSlots.empty());

        if (hasSubscriptions && SubscriptionsHideActive)
        {
            ImGui::TextDisabled("Nothing upcoming — everything");
            ImGui::TextDisabled("subscribed is currently active.");
        }
        else if (hasSubscriptions)
        {
            // Reachable when every subscribed Core Boss has already been
            // killed today (see IsWorldBossCompletedToday above) and/or
            // every subscribed meta-event slot's map chest has already
            // been claimed today (see IsMapChestClaimedToday above), and
            // nothing else is subscribed — distinct from "no
            // subscriptions at all" below, since the fix here isn't
            // "check a box", it's "wait for reset".
            ImGui::TextDisabled("Nothing to show — everything");
            ImGui::TextDisabled("subscribed is already done today.");
        }
        else
        {
            ImGui::TextDisabled("No subscribed events yet.");
            ImGui::TextDisabled("Check the box next to an event's name");
            ImGui::TextDisabled("in the options panel to add it here.");
        }
    }
    else
    {
        for (const auto& row : rows)
            DrawSubscriptionRow(row.name, row.chatCode, row.active, row.secs, row.isWeekly);

        ImGui::Separator();
        ImGui::TextDisabled("Click a row to copy its waypoint code.");
    }

    ImGui::End();
}
