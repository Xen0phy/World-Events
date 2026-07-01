// subscriptions_window.cpp
// Draws the standalone "Subscriptions" watchlist window.

#include "subscriptions_window.h"
#include "subscriptions.h"
#include "events.h"
#include "cyclic.h"
#include "maprender.h"
#include "settings.h"
#include "imgui.h"
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

        int secondsOfDay = (int)(now % 86400);
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
// DrawStatusText
// ---------------------------------------------------------------------------
// Shared "Active (ends in ...)" / "in Xh Ym" / "in Xm Ys" formatting,
// matching the wording already used in the map/ring tooltips so the
// watchlist reads consistently with the rest of the addon. Three text
// colors: SubscriptionsActiveColor while active, SubscriptionsSoonColor
// for anything starting within kSoonThresholdSecs, and the window's
// normal default text color otherwise — deliberately no third
// configurable "waiting" color the way the map markers have one, since
// everything past the soon-window reads fine as plain text and doesn't
// need to compete for the user's attention.
// ---------------------------------------------------------------------------
static void DrawStatusText(const std::string& name, bool active, int secs)
{
    if (active)
    {
        ImGui::TextColored(SubscriptionColorToImVec4(SubscriptionsActiveColor),
            "%s — Active (ends in %dm %02ds)",
            name.c_str(), secs / 60, secs % 60);
    }
    else if (secs < kSoonThresholdSecs)
    {
        ImGui::TextColored(SubscriptionColorToImVec4(SubscriptionsSoonColor),
            "%s — in %dm %02ds", name.c_str(), secs / 60, secs % 60);
    }
    else if (secs >= 3600)
    {
        ImGui::Text("%s — in %dh %02dm", name.c_str(), secs / 3600, (secs % 3600) / 60);
    }
    else
    {
        ImGui::Text("%s — in %dm %02ds", name.c_str(), secs / 60, secs % 60);
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
    struct Row { std::string name; bool active; int secs; };
    std::vector<Row> rows;
    rows.reserve(g_SubscribedBasicEvents.size() + g_SubscribedCyclicSlots.size());

    for (const auto& evName : g_SubscribedBasicEvents)
    {
        auto it = std::find_if(g_Events.begin(), g_Events.end(),
            [&](const WorldEvent& ev) { return ev.name == evName; });
        if (it == g_Events.end()) continue; // deleted since subscribing — skip, leave the subscription alone

        bool active = IsEventActive(*it, now);
        int  secs   = active ? GetSecondsUntilEventEnd(*it, now) : GetSecondsUntilEventStart(*it, now);
        if (secs < 0) continue; // no timer data yet
        if (active && SubscriptionsHideActive) continue; // "only show what's not already happening"

        rows.push_back({ it->name, active, secs });
    }

    for (const auto& key : g_SubscribedCyclicSlots)
    {
        auto it = std::find_if(g_CyclicGroups.begin(), g_CyclicGroups.end(),
            [&](const CyclicGroup& grp) { return grp.name == key.groupName; });
        if (it == g_CyclicGroups.end()) continue; // group deleted since subscribing

        SlotStatus status = GetCyclicSlotStatus(*it, key.slotOffset, now);
        if (!status.found) continue; // slot deleted/re-offset since subscribing
        if (status.active && SubscriptionsHideActive) continue;

        // Prefix with the group name so two groups' same-named slots
        // (e.g. Dry Top's two "Crash Site" occurrences) stay
        // distinguishable in the flat watchlist.
        for (const auto& slot : it->slots)
        {
            if (slot.offset != key.slotOffset) continue;
            rows.push_back({ it->name + " — " + slot.name, status.active, status.secs });
            break;
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
        if (SubscriptionsHideActive && !(g_SubscribedBasicEvents.empty() && g_SubscribedCyclicSlots.empty()))
        {
            ImGui::TextDisabled("Nothing upcoming — everything");
            ImGui::TextDisabled("subscribed is currently active.");
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
            DrawStatusText(row.name, row.active, row.secs);
    }

    ImGui::End();
}
