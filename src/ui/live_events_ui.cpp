//################################################################################
// live_events_ui.cpp   (see: live_events_ui.h)
//--------------------------------------------------------------------------------
// Layout/timing constants below mirror subscriptions_notification.cpp's toast
// stack (same "borderless ImGui window positioned by screen-space math, refreshed
// every frame" approach), since this is the same kind of transient corner overlay
// - just interactive (a real ImGui::Button, not an invisible hit-test region) and
// gated by proximity instead of a timer.
//--------------------------------------------------------------------------------

#include "addon.h"
#include "events_live.h"
#include "gw2_api.h" //. GetLiveEventsRegion, for the UpdateNotificationState call below
#include "imgui.h"
#include "live_events_ui.h"
#include "notification_client.h" //. UpdateNotificationState, GetRegionViewerCount
#include "settings.h"
#include "shard_id.h"
#include "subscriptions.h" //. GetMumbleCharacterName, read when ShareNameInReports is on
#include "time_format.h"
#include "ws_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

//_ Button stack layout, screen-space pixels - see kMarginX/Y etc in subscriptions_notification.cpp for the sibling convention. Anchor position itself (LiveEventButtonMarginX/Y) is user-adjustable - see settings_table.h.
static constexpr float kButtonWidth  = 220.0f;
static constexpr float kButtonHeight = 32.0f;
static constexpr float kGapY         = 6.0f;   //. vertical gap between stacked buttons

//_ Shard-update throttle interval - see RenderLiveEventButtons.
static constexpr unsigned long long kShardUpdateIntervalMs = 1000;

//_ Fixed, not user-adjustable - a client-settable cooldown could be set to 0 by anyone motivated to spam.
static constexpr unsigned long long kReportCooldownMs = 30000;

//_ Last report-button press per event id, GetTickCount64() ticks - see RenderLiveEventButtons.
static std::unordered_map<std::string, unsigned long long> s_lastReportPressMs;

bool LiveEventButtonMoveMode = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtonMovePreview   (pairs with: RenderLiveEventButtons)
//--------------------------------------------------------------------------------
// The one draggable stand-in RenderLiveEventButtons shows while
// LiveEventButtonMoveMode is on, clamped so it can't be dragged off screen.
// Tracks the mouse delta itself - not ImGui's own window-move - so dragging works
// the same regardless of io.ConfigWindowsMoveFromTitleBarOnly.
//--------------------------------------------------------------------------------
static void RenderLiveEventButtonMovePreview()
{
    ImGuiIO& io = ImGui::GetIO();
    float x = io.DisplaySize.x - LiveEventButtonMarginX - kButtonWidth;
    float y = LiveEventButtonMarginY;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(kButtonWidth, kButtonHeight));
    ImGui::SetNextWindowBgAlpha(0.0f); //. background drawn by Button below
    ImGui::Begin("##we_live_btn_move_preview", nullptr,
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoResize           |
        ImGuiWindowFlags_NoMove             |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);

    ImGui::Button("Drag to move##we_live_btn_move_preview", ImVec2(kButtonWidth, kButtonHeight));

    static bool   s_dragging = false;
    static ImVec2 s_dragStartMouse;
    static ImVec2 s_dragStartMargin;

    if (ImGui::IsItemActivated())
    {
        s_dragging        = true;
        s_dragStartMouse  = io.MousePos;
        s_dragStartMargin = ImVec2(LiveEventButtonMarginX, LiveEventButtonMarginY);
    }
    if (s_dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        float dx = io.MousePos.x - s_dragStartMouse.x;
        float dy = io.MousePos.y - s_dragStartMouse.y;

        //_ Margin is measured from the right edge, so dragging right shrinks it.
        float newMarginX = s_dragStartMargin.x - dx;
        float newMarginY = s_dragStartMargin.y + dy;

        float maxMarginX = io.DisplaySize.x - kButtonWidth;
        float maxMarginY = io.DisplaySize.y - kButtonHeight;
        LiveEventButtonMarginX = newMarginX < 0.0f ? 0.0f : (newMarginX > maxMarginX ? maxMarginX : newMarginX);
        LiveEventButtonMarginY = newMarginY < 0.0f ? 0.0f : (newMarginY > maxMarginY ? maxMarginY : newMarginY);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        s_dragging = false;

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Drag to reposition the live-event report button.\n"
                           "Untick \"Move button\" in options when done.");
    }

    ImGui::End();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons   (group: OpenLiveEventReportsWindow, RenderLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header. MumbleLink/NexusLink are null-checked here too - addon.cpp's
// AddonRender already gates on both, but this file doesn't assume that ordering
// holds forever. UpdateShard runs regardless of LiveEventsSubscribed, throttled
// to ~1x/sec (the render-tick hook networking-handoff.md section 9 #5 calls for),
// so unticking it disconnects an open shard within ~1s. Only issued with a real
// shard when liveEventsReady (== LiveEventsSubscribed, no API key needed) AND
// MapHasLiveEvents (events_live.h) agree; a default ShardIdentity is sent
// otherwise, a no-op if already disconnected. UpdateNotificationState
// (notification_client.h) rides the same tick, self-gated on GetLiveEventsRegion
// (gw2_api.h) - an empty key only drops the toast relay.
//--------------------------------------------------------------------------------
void RenderLiveEventButtons()
{
    if (!MumbleLink || !NexusLink || !NexusLink->IsGameplay) return;

    bool liveEventsReady = LiveEventsSubscribed;

    static unsigned long long s_lastShardUpdateMs = 0;
    unsigned long long nowMs = GetTickCount64();
    if (nowMs - s_lastShardUpdateMs >= kShardUpdateIntervalMs)
    {
        if (liveEventsReady && MapHasLiveEvents((int)MumbleLink->Context.MapID))
            UpdateShard(ComputeShardIdentity(MumbleLink->Context));
        else
            UpdateShard(ShardIdentity{});
        UpdateNotificationState(GetLiveEventsRegion());
        s_lastShardUpdateMs = nowMs;
    }

    if (LiveEventButtonMoveMode)
    {
        RenderLiveEventButtonMovePreview();
        return;
    }

    if (!liveEventsReady) return;

    //_ Every compiled-in LiveEvent counts once subscribed - no per-event opt-in (see events_live.h).
    std::vector<const LiveEvent*> nearby;
    for (const LiveEvent& ev : g_LiveEvents)
    {
        if (!IsPlayerNearLiveEvent(ev, *MumbleLink)) continue;
        nearby.push_back(&ev);
    }
    if (nearby.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float x = io.DisplaySize.x - LiveEventButtonMarginX - kButtonWidth;

    for (size_t i = 0; i < nearby.size(); i++)
    {
        const LiveEvent* ev = nearby[i];
        float y = LiveEventButtonMarginY + (float)i * (kButtonHeight + kGapY);

        //_ Keyed by eventId, not loop index, so a button's window identity stays stable if the nearby list's order shifts between frames.
        std::string winId = "##we_live_btn_" + ev->eventId;

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(kButtonWidth, kButtonHeight));
        ImGui::SetNextWindowBgAlpha(0.0f); //. background drawn by Button below
        ImGui::Begin(winId.c_str(), nullptr,
            ImGuiWindowFlags_NoTitleBar         |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_NoMove             |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoSavedSettings    |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav);

        unsigned long long nowTick = GetTickCount64();

        auto it = s_lastReportPressMs.find(ev->eventId);
        unsigned long long sinceLastMs = (it != s_lastReportPressMs.end()) ? (nowTick - it->second) : kReportCooldownMs;
        bool onCooldown = sinceLastMs < kReportCooldownMs;

        std::string label = ev->name + "##we_live_report_" + ev->eventId;
        if (onCooldown) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        bool clicked = ImGui::Button(label.c_str(), ImVec2(kButtonWidth, kButtonHeight));
        if (onCooldown) ImGui::PopStyleVar();

        if (clicked)
        {
            if (!onCooldown)
            {
                SendReport(ev->eventId, ShareNameInReports ? GetMumbleCharacterName() : std::string());
                s_lastReportPressMs[ev->eventId] = nowTick;
            }
            OpenLiveEventReportsWindow(); //. opens either way - right-click already does this without sending
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            OpenLiveEventReportsWindow();
        }
        if (ImGui::IsItemHovered())
        {
            if (onCooldown)
            {
                unsigned long long remainingSec = (kReportCooldownMs - sinceLastMs + 999) / 1000;
                ImGui::SetTooltip("Reported recently - %llu s before you can report \"%s\" again.\n"
                                   "Right-click: just show recent reports.", remainingSec, ev->name.c_str());
            }
            else
            {
                ImGui::SetTooltip("Click: report \"%s\" as active and show recent reports.\n"
                                   "Right-click: just show recent reports, without reporting.", ev->name.c_str());
            }
        }

        ImGui::End();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenLiveEventReportsWindow   (group: RenderLiveEventButtons, RenderLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header.
//--------------------------------------------------------------------------------
void OpenLiveEventReportsWindow()
{
    ShowLiveEventReportsWindow = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ConnectionStateLabel   (pairs with: ConnStateLabel in ws_debug_window.cpp)
//--------------------------------------------------------------------------------
// Plain text for GetConnectionState() (ws_client.h), for the small status line at
// the top of the reports window - lets a player tell "no reports yet" apart from
// "not even connected right now."
//--------------------------------------------------------------------------------
static const char* ConnectionStateLabel(WsConnectionState state)
{
    switch (state)
    {
        case WsConnectionState::Connected:  return "Connected";
        case WsConnectionState::Connecting: return "Connecting...";
        default:                            return "Disconnected";
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventReportsWindow   (group: RenderLiveEventButtons, OpenLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header. GetRecentReports is already newest-first (ws_client.h): its first
// entry folds into each row's idLine to form that row's own tree label, the rest
// become leaves underneath, so no re-sort is needed here either way. Filters
// g_LiveEvents by MumbleLink->Context.MapID - a report can still be worth
// checking on a shard from across the map, not just in range.
// LiveEventReportsWindowLocked (settings_table.h) strips the window down to bare,
// click-through text pinned at its last position, deregistering Escape- to-close
// while locked - see "Lock window" in the options panel. The region- viewer
// suffix is a separate connection (notification_client.h) from the "Server:"
// line's shard connection (ws_client.h) - the two can disagree.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow()
{
    //_ Mirrors AddonLoad's unconditional initial registration - corrects itself below on this function's first call if the loaded setting says otherwise.
    static bool s_escapeCloseRegistered = true;
    if (LiveEventReportsWindowLocked && s_escapeCloseRegistered)
    {
        APIDefs->GUI_DeregisterCloseOnEscape(kLiveEventReportsWindowTitle);
        s_escapeCloseRegistered = false;
    }
    else if (!LiveEventReportsWindowLocked && !s_escapeCloseRegistered)
    {
        APIDefs->GUI_RegisterCloseOnEscape(kLiveEventReportsWindowTitle, &ShowLiveEventReportsWindow);
        s_escapeCloseRegistered = true;
    }

    if (!ShowLiveEventReportsWindow) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (LiveEventReportsWindowLocked)
    {
        ImGui::SetNextWindowBgAlpha(0.0f); //. background drawn by nothing - see flags below
        flags |= ImGuiWindowFlags_NoTitleBar          |
                 ImGuiWindowFlags_NoScrollbar          |
                 ImGuiWindowFlags_NoBackground         |
                 ImGuiWindowFlags_NoInputs             |
                 ImGuiWindowFlags_NoBringToFrontOnFocus;
    }

    ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kLiveEventReportsWindowTitle, &ShowLiveEventReportsWindow, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Shard: %s", ConnectionStateLabel(GetConnectionState()));

    ImGui::TextDisabled("Region: %s", ConnectionStateLabel(GetNotificationConnectionState()));
    std::optional<int> regionViewers = GetRegionViewerCount();
    if (regionViewers)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d online in %s)", *regionViewers,
            LiveEventsRegionToWireString(GetLiveEventsRegion()).c_str());
    }

    if (!MumbleLink)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Not in game.");
        ImGui::End();
        return;
    }

    int mapId = (int)MumbleLink->Context.MapID;
    std::optional<uint8_t> octet = GetShardLastAddressOctet(MumbleLink->Context);
    time_t now = time(nullptr);
    bool any = false;

    for (const LiveEvent& ev : g_LiveEvents)
    {
        if (ev.mapId != mapId) continue;
        any = true;

        std::string idLine = ev.name;
        if (octet)
            idLine += "." + std::to_string(*octet);

        std::vector<EventReport> reports = GetRecentReports(ev.eventId);
        if (reports.empty())
        {
            ImGui::TextUnformatted((idLine + " (empty)").c_str());
            continue;
        }

        //_ Signed/clamped the same way subscriptions_notification.cpp treats its own tick-based elapsed time - a server-stamped ts should never be in the future, but a client clock can't be trusted not to disagree slightly.
        long long elapsedSigned = (long long)now - reports.front().timestampUnix; //. newest first, see GetRecentReports
        int elapsed = elapsedSigned > 0 ? (int)elapsedSigned : 0;
        std::string treeLabel = idLine + " (" + FormatMinSec(elapsed) + " ago)";

        if (reports.size() == 1)
        {
            ImGui::TextUnformatted(treeLabel.c_str()); //. nothing to fold with only one report
            continue;
        }

        //_ Keyed by eventId so every event's tree keeps its own fold state.
        std::string treeId = "##we_live_reports_tree_" + ev.eventId;
        if (ImGui::TreeNode(treeId.c_str(), "%s", treeLabel.c_str()))
        {
            for (size_t i = 1; i < reports.size(); i++)
            {
                long long es = (long long)now - reports[i].timestampUnix;
                int e = es > 0 ? (int)es : 0;
                ImGui::BulletText("%s ago", FormatMinSec(e).c_str());
            }
            ImGui::TreePop();
        }
    }

    if (!any)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("No live events on this map.");
    }

    ImGui::End();
}