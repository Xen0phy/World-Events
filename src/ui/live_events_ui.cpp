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
#include "imgui.h"
#include "live_events_ui.h"
#include "settings.h"
#include "shard_id.h"
#include "time_format.h"
#include "ws_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctime>
#include <string>
#include <vector>

//_ Button stack layout, screen-space pixels - see kMarginX/Y etc in subscriptions_notification.cpp for the sibling convention.
static constexpr float kButtonWidth  = 220.0f;
static constexpr float kButtonHeight = 32.0f;
static constexpr float kGapY         = 6.0f;   //. vertical gap between stacked buttons
static constexpr float kMarginX      = 20.0f;  //. from the right screen edge
static constexpr float kMarginY      = 20.0f;  //. from the top screen edge

//_ Shard-update throttle interval - see RenderLiveEventButtons.
static constexpr unsigned long long kShardUpdateIntervalMs = 1000;

bool ShowLiveEventReportsWindow = false;

//_ Which event the reports window is currently targeted at - set by OpenLiveEventReportsWindow, read by RenderLiveEventReportsWindow.
static std::string s_reportsWindowEventId;
static std::string s_reportsWindowEventName;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons   (group: OpenLiveEventReportsWindow, RenderLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header. MumbleLink/NexusLink are null-checked here too - addon.cpp's
// AddonRender already gates on both, but this file doesn't assume that ordering
// holds forever. UpdateShard is throttled to ~1x/sec (the render- tick hook
// networking-handoff.md section 9 #5 calls for); harmless to call more often
// since it's itself a no-op on an unchanged shard key.
//--------------------------------------------------------------------------------
void RenderLiveEventButtons()
{
    if (!ShowLiveEventButton) return;
    if (!MumbleLink || !NexusLink || !NexusLink->IsGameplay) return;

    static unsigned long long s_lastShardUpdateMs = 0;
    unsigned long long nowMs = GetTickCount64();
    if (nowMs - s_lastShardUpdateMs >= kShardUpdateIntervalMs)
    {
        UpdateShard(ComputeShardIdentity(MumbleLink->Context));
        s_lastShardUpdateMs = nowMs;
    }

    //_ IsPlayerNearLiveEvent already checks event.mapId against MumbleLink->Context.MapID internally - see events_live.h.
    std::vector<const LiveEvent*> nearby;
    for (const LiveEvent& ev : g_LiveEvents)
    {
        if (!IsLiveEventActivated(ev.eventId))      continue;
        if (!IsPlayerNearLiveEvent(ev, *MumbleLink)) continue;
        nearby.push_back(&ev);
    }
    if (nearby.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float x = io.DisplaySize.x - kMarginX - kButtonWidth;

    for (size_t i = 0; i < nearby.size(); i++)
    {
        const LiveEvent* ev = nearby[i];
        float y = kMarginY + (float)i * (kButtonHeight + kGapY);

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

        std::string label = ev->name + "##we_live_report_" + ev->eventId;
        if (ImGui::Button(label.c_str(), ImVec2(kButtonWidth, kButtonHeight)))
        {
            SendReport(ev->eventId);
            OpenLiveEventReportsWindow(ev->eventId, ev->name);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            OpenLiveEventReportsWindow(ev->eventId, ev->name);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click: report \"%s\" as active and show recent reports.\n"
                               "Right-click: just show recent reports, without reporting.", ev->name.c_str());
        }

        ImGui::End();
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenLiveEventReportsWindow   (group: RenderLiveEventButtons, RenderLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header.
//--------------------------------------------------------------------------------
void OpenLiveEventReportsWindow(const std::string& eventId, const std::string& eventName)
{
    s_reportsWindowEventId   = eventId;
    s_reportsWindowEventName = eventName;
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
// See header. GetRecentReports is already newest-first (ws_client.h), so rows are
// drawn in the order returned with no re-sort here.
//--------------------------------------------------------------------------------
void RenderLiveEventReportsWindow()
{
    if (!ShowLiveEventReportsWindow) return;

    ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kLiveEventReportsWindowTitle, &ShowLiveEventReportsWindow))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(s_reportsWindowEventName.c_str());
    ImGui::TextDisabled("Server: %s", ConnectionStateLabel(GetConnectionState()));
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<EventReport> reports = GetRecentReports(s_reportsWindowEventId);
    if (reports.empty())
    {
        ImGui::TextDisabled("No reports yet for this event on your map instance.");
    }
    else
    {
        time_t now = time(nullptr);
        for (const EventReport& r : reports)
        {
            //_ Signed/clamped the same way subscriptions_notification.cpp treats its own tick-based elapsed time - a server-stamped ts should never be in the future, but a client clock can't be trusted not to disagree slightly.
            long long elapsedSigned = (long long)now - r.timestampUnix;
            int elapsed = elapsedSigned > 0 ? (int)elapsedSigned : 0;
            ImGui::Text("Reported %s ago", FormatMinSec(elapsed).c_str());
        }
    }

    ImGui::End();
}