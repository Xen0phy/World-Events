// subscriptions_window.cpp
// Draws the standalone "Subscriptions" watchlist window.

#include "subscriptions.h"
#include "subscriptions_cache.h"
#include "events_tracking.h"
#include "settings.h"
#include "imgui.h"
#include "addon.h" // SubsWindowDataTimer/SubsWindowDrawTimer — see their comment in addon.h
#include <windows.h>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// SubscriptionColorToImVec4
// ---------------------------------------------------------------------------
// SubscriptionsActiveColor/SoonColor are stored as packed RRGGBBAA (R in
// the top byte — same convention as ColorSet::base in events.h and the
// map's BasicEventColor* settings), which is NOT the same byte order as
// ImGui's own ImU32 (ABGR). The low byte (alpha) is deliberately ignored
// here: these feed straight into ImGui::TextColored, which is plain text
// with a fixed opacity of 1.0, not a translucency-capable draw.
// ---------------------------------------------------------------------------
static ImVec4 SubscriptionColorToImVec4(unsigned int rgba)
{
    return ImVec4(
        ((rgba >> 24) & 0xFF) / 255.0f, // R
        ((rgba >> 16) & 0xFF) / 255.0f, // G
        ((rgba >>  8) & 0xFF) / 255.0f, // B
        1.0f                             // alpha channel of `rgba` unused
    );
}

// 15 minutes — matches the "soon" threshold already hardcoded for
// BasicEventColorSoon on the map (maprender.cpp's `secs < 900` check),
// reused here rather than adding a second configurable window.
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
// s_leftPressedKey
// ---------------------------------------------------------------------------
// A manual IsMouseClicked() check fires the instant a button goes down
// while hovering an item — Selectable's default gesture is press-AND-
// release on the SAME item (ImGuiButtonFlags_PressedOnClickRelease), so a
// press that then drags off the row before releasing does nothing. This
// tracks which row's name a left-button press started on, so
// DrawSubscriptionRow can require the release to land back on that same
// row before actually acting — matching Selectable's own return-value
// behavior instead of firing on mouse-down.
//
// Right-click has no equivalent tracker: the original code used
// IsItemClicked(Right), which fires on mouse-down while hovering (it's
// literally IsMouseClicked() && IsItemHovered(), not release-gated), so
// the rewritten right-click check mirrors that directly with no press/
// release state needed.
// ---------------------------------------------------------------------------
static std::string s_leftPressedKey;

// ---------------------------------------------------------------------------
// DrawSubscriptionRow
// ---------------------------------------------------------------------------
// Draws one watchlist row: click copies "<n>: <chatCode>" to the clipboard
// (or just "<n>" if no chat code is set yet, rather than leaving a dangling
// "Name: " with an empty tail).
//
// This used to be a single ImGui::Selectable per row — simple, but a
// Selectable is a full interactive item (ID hash off the label, hover/
// active bookkeeping, click state machine) paid for on EVERY row EVERY
// frame regardless of whether the mouse is anywhere near it. That's why
// this window cost roughly 2x the subscription bar per frame despite
// being the visually simpler of the two: the bar already draws its
// resting state with plain ImDrawList calls (see subscriptions_bar.cpp)
// and only creates a real interactive item for whichever segment is
// actually under the mouse.
//
// Same idea here: the row rect is computed manually, hover is a plain
// IsMouseHoveringRect() bounding-box test (no widget), and the text/
// highlight are drawn directly via the window's draw list. Layout/scroll
// space is still reserved with a plain Dummy() (ItemAdd only, no ID, no
// interaction) so the window's content size and scrollbar behave exactly
// as before. Click/right-click are then just plain mouse-state checks
// gated on that same hover test — no per-row widget ever gets created.
//
// Text color follows the Active/Soon/default rule; a just-clicked row
// additionally flashes a bright highlight for kFlashDurationMs as click
// confirmation (see s_flashKey/s_flashUntil above) — chosen instead of a
// tooltip so the confirmation doesn't cover the text just clicked, and
// instead of a persistent "Copied!" label so the window doesn't visually
// shift/grow when clicked.
// ---------------------------------------------------------------------------
static void DrawSubscriptionRow(const std::string& name, const std::string& chatCode, bool active, int secs, bool isWeekly,
    bool isBasic, const std::string& basicName, const CyclicSubscriptionKey& cyclicKey)
{
    if (isWeekly)
    {
        // Small red dot marking this row as an active-and-incomplete
        // weekly Wizard's Vault target this week — see weekly_vault.h/
        // .cpp for what sets isWeekly and the event/slot -> objective
        // mapping table. Purely visual; doesn't affect the row's click
        // behavior below. Plain TextColored: an item, but not an
        // interactive one, so its cost is negligible next to a Selectable.
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
        snprintf(buf, sizeof(buf), " -- Active (ends in %dm %02ds)", secs / 60, secs % 60);
        statusSuffix = buf;
        color = SubscriptionColorToImVec4(SubscriptionsActiveColor);
    }
    else if (secs < kSoonThresholdSecs)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), " -- in %dm %02ds", secs / 60, secs % 60);
        statusSuffix = buf;
        color = SubscriptionColorToImVec4(SubscriptionsSoonColor);
    }
    else
    {
        char buf[32];
        if (secs >= 3600)
            snprintf(buf, sizeof(buf), " -- in %dh %02dm", secs / 3600, (secs % 3600) / 60);
        else
            snprintf(buf, sizeof(buf), " -- in %dm %02ds", secs / 60, secs % 60);
        statusSuffix = buf;
        useColor = false;
    }

    std::string label = name + statusSuffix;

    // ---------------------------------------------------------------------
    // Row rect + hover test, computed BEFORE drawing anything, same as
    // subscriptions_bar.cpp works out hoveredIndices before drawing.
    // ---------------------------------------------------------------------
    ImVec2 rowMin       = ImGui::GetCursorScreenPos();
    float  rowWidth     = ImGui::GetContentRegionAvail().x;
    // Selectable(label, ..., size=(0,0)) sizes itself to CalcTextSize(label).y
    // — i.e. just the text line height, with none of a Button's/Frame's
    // FramePadding.y*2 added on top. GetFrameHeight() would be taller than
    // what Selectable actually used, so rows must match that exactly here
    // or every row (and the window's whole content height) grows.
    float  rowHeight    = ImGui::GetTextLineHeight();
    ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowHeight);

    // IsMouseHoveringRect already clips against the window's own clip rect
    // (scrolling-safe); IsWindowHovered gates it so this window doesn't
    // register a hover while something else (including one of this
    // window's own popups) sits on top of it — same protection
    // Selectable gave for free, restored here explicitly since the manual
    // rect test has no built-in awareness of popups blocking it.
    bool hovered = ImGui::IsWindowHovered()
        && ImGui::IsMouseHoveringRect(rowMin, rowMax);

    bool flashing = (s_flashKey == name) && (GetTickCount64() < s_flashUntil);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background highlight: bright white flash takes priority (see the
    // click-confirmation note above) over the plain hover highlight a
    // Selectable would have given for free.
    if (flashing)
        dl->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 90));
    else if (hovered)
        dl->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    ImU32 textColor = useColor ? ImGui::GetColorU32(color) : ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 textPos(rowMin.x, rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    dl->AddText(textPos, textColor, label.c_str());

    // Reserve the row's layout/scroll space. Dummy only does ItemSize +
    // a no-ID ItemAdd — none of Selectable's ID hashing or click/hover
    // state machine — so the window's content height/scrollbar still
    // come out exactly right without paying for a widget nothing needs.
    ImGui::Dummy(ImVec2(rowWidth, rowHeight));

    // Press-and-release-on-the-SAME-row, matching Selectable's default
    // ImGuiButtonFlags_PressedOnClickRelease gesture: record which row a
    // press started on, only act if the release also lands back on that
    // row. A press that drags off before releasing does nothing, same as
    // before this rewrite.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s_leftPressedKey = name;
    if (hovered && s_leftPressedKey == name && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        std::string toCopy = BuildChatPasteMessage(name, chatCode);
        PasteToChat(toCopy, std::chrono::milliseconds(delayMilliseconds));

        s_flashKey   = name;
        s_flashUntil = GetTickCount64() + kFlashDurationMs;
    }

    // Right-click: mark this event/slot done for today. Hides it from
    // this window (and the bar/toast) until the next UTC daily reset, or
    // until "Clear all manual done markers" is used in the options panel —
    // see events_tracking.h. Popup ID is keyed off `name`, same as
    // s_flashKey above, so it's unique per row without needing a
    // separately-tracked numeric ID.
    //
    // Unlike the left-click paste above, this fires on mouse-DOWN while
    // hovering, not on release: the original code used IsItemClicked(),
    // which is defined as IsMouseClicked() && IsItemHovered() — a press-
    // based check, NOT release-gated like Selectable's own return value.
    // Making this release-gated too (as an earlier version of this rewrite
    // did) would have been a real behavior change, not just a faithful
    // port.
    std::string popupId = "##we_done_popup_" + name;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup(popupId.c_str());

    // Called every frame regardless of hover — an already-open popup has
    // to keep rendering even after the mouse moves off this row, exactly
    // as BeginPopup did before this change. The lookup itself is just an
    // ID hash/compare against the popup stack, cheap even when it's not
    // this row's popup that's open.
    if (ImGui::BeginPopup(popupId.c_str()))
    {
        if (ImGui::Selectable("Mark done for today"))
        {
            if (isBasic) ToggleBasicEventDoneToday(basicName);
            else         ToggleCyclicSlotDoneToday(cyclicKey);
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// RenderSubscriptionsWindow
// ---------------------------------------------------------------------------
// Sortable, unified list of rows across both Basic Events and Cyclic slots
// so "what's coming up soonest" reads as one list rather than two separate
// sections the user has to visually merge themselves — active entries
// first, then soonest-upcoming, matching the sort already used for the
// per-group tooltip in cyclicrender.cpp. isBasic/basicName/cyclicKey:
// whichever pair is relevant identifies this row for
// ToggleBasicEventDoneToday/ToggleCyclicSlotDoneToday — see the right-click
// "Mark done for today" menu in DrawSubscriptionRow.
// ---------------------------------------------------------------------------
struct Row { std::string name; std::string chatCode; bool active; int secs; bool isWeekly;
             bool isBasic = true; std::string basicName; CyclicSubscriptionKey cyclicKey; };

void RenderSubscriptionsWindow()
{
    if (!ShowSubscriptionsWindow) return;

    time_t now = time(nullptr);
    std::vector<Row> rows;
    {
        // Scoped to just data gathering/resolution — see SubsBarDataTimer's
        // equivalent comment in subscriptions_bar.cpp for why this is split
        // from SubsWindowDrawTimer below.
        SubsWindowDataTimer dataTimer; // no-op unless ShowDebug — see addon.h
        RefreshSubscriptionsCache(now); // no-op most frames — see subscriptions_cache.h

        const auto& resolved = GetResolvedSubscriptions(); // shared cache, built once and reused by the bar/notifications too
        rows.reserve(resolved.size());

        for (const auto& sub : resolved)
        {
            if (sub.doneToday) continue; // API-confirmed OR manually marked — see ResolvedSubscription::doneToday

            SubscriptionActiveState as = GetSubscriptionActiveState(sub, now);
            int secs = as.active ? as.secsUntilEnd : as.secsUntilStart;
            if (secs < 0) continue; // no timer data yet
            if (as.active && SubscriptionsHideActive) continue; // "only show what's not already happening"

            rows.push_back({ sub.label, sub.chatCode, as.active, secs, sub.isWeeklyTarget,
                              sub.isBasic, sub.basicName, CyclicSubscriptionKey{ sub.cyclicGroupName, sub.cyclicSlotOffset } });
        }

        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b)
        {
            if (a.active != b.active) return a.active; // active first
            return a.secs < b.secs;                    // then soonest first
        });
    }

    // Everything from here on is the actual ImGui window/row rendering —
    // see g_AvgSubsWindowDrawMs's comment in addon.h.
    SubsWindowDrawTimer drawTimer; // no-op unless ShowDebug — see addon.h

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
            DrawSubscriptionRow(row.name, row.chatCode, row.active, row.secs, row.isWeekly,
                                 row.isBasic, row.basicName, row.cyclicKey);

        ImGui::Separator();
        ImGui::TextDisabled("Click a row to copy its waypoint code.");
        ImGui::TextDisabled("Right-click to mark done for today.");
    }

    // A press that started on a row but got released somewhere that isn't
    // any row (a gap, off the window entirely, etc.) never matched inside
    // DrawSubscriptionRow's own check above — clear it here once so a
    // stale key can't wrongly match a future row that happens to share the
    // same name.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_leftPressedKey.clear();

    ImGui::End();
}
