//################################################################################
// options_live.cpp   (see: options_live.h)
//--------------------------------------------------------------------------------
// LiveEventButtonMoveMode           transient "drag to reposition" flag
// RenderLiveEventButtonMovePreview  draggable stand-in for the button stack
// ConnectionStateLabel              plain text for a WsConnectionState
// DrawColoredWrapped                wrapped text in one color
// DrawLink                          clickable wrapped text
// DrawSubscribe                     switch, key/region warning, Help link
// DrawDisplay                       display toggles, name sharing, report color
// DrawLiveEventRow                  one row of the event list
// DrawEventList                     roster note and the event table
//--------------------------------------------------------------------------------
// Overlay: the in-game half of the live-event reporting feature, report
// submission and the recent-reports popup, on top of the wire/storage layer
// ws_client.h and events_live.h provide. Its layout/timing constants mirror
// subscriptions_notification.cpp's toast stack (same "borderless ImGui window
// positioned by screen-space math, refreshed every frame" approach), since this
// is the same kind of transient corner overlay - just interactive (a real
// ImGui::Button, not an invisible hit-test region) and gated by proximity instead
// of a timer.
//
// Tab: three groups drawn straight into the content child - the subscribe switch
// with its warnings, the display toggles, and the event table - separated by
// spacing only. Text that can outgrow the window's minimum width wraps instead of
// clipping. The table is the one place a Live deep link lands: its row is
// scrolled to and flashed through OptionsHighlight_Set (options_window.h).
//--------------------------------------------------------------------------------

#include "options_live.h"

#include "addon.h"
#include "addon_options_helpers.h" //. Tooltip, DisabledBlock, DrawSubscribeCheckbox
#include "events_live.h"
#include "events_tracking.h" //. IsLiveEventMarkedDoneToday/ToggleLiveEventDoneToday, for the Done today column
#include "gw2_api.h" //. GetLiveEventsRegion, for the UpdateNotificationState call and the region warning
#include "imgui.h"
#include "localization.h"
#include "notification_client.h" //. UpdateNotificationState, GetRegionViewerCount
#include "options_general.h" //. RequestOpenAccountHeader, for the Set API key link
#include "settings.h"
#include "shard_id.h"
#include "subscriptions.h" //. GetMumbleCharacterName, read when ShareNameInReports is on
#include "time_format.h"
#include "ws_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
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

//_ Transient (an editing mode, not state worth persisting): set by the Move button checkbox, read by RenderLiveEventButtons.
static bool LiveEventButtonMoveMode = false;

//_ Warning line color.
static const ImVec4 kWarningColor(1.0f, 0.6f, 0.2f, 1.0f);

//_ Link text color; links have no underline, so this alone marks them as clickable.
static const ImVec4 kLinkColor(0.4f, 0.7f, 1.0f, 1.0f);

//_ Swatch button only, no numeric fields; the click opens a hue-wheel picker.
static constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;

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

    ImGui::Button(TrId("WE_LIVE_DRAG_BUTTON", "##we_live_btn_move_preview").c_str(), ImVec2(kButtonWidth, kButtonHeight));

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
        ImGui::SetTooltip("%s", Tr("WE_LIVE_DRAG_TOOLTIP"));
    }

    ImGui::End();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderLiveEventButtons   (group: OpenLiveEventReportsWindow, RenderLiveEventReportsWindow)
//--------------------------------------------------------------------------------
// See header. MumbleLink/NexusLink are null-checked here too - addon.cpp's
// AddonRender already gates on both, but this file doesn't assume that ordering
// holds forever. UpdateShard runs regardless of LiveEventsSubscribed, throttled
// to ~1x/sec here, so unticking it disconnects an open shard within ~1s. Only
// issued with a real shard when liveEventsReady (== LiveEventsSubscribed, no API
// key needed) AND MapHasLiveEvents (events_live.h) agree; a default ShardIdentity
// is sent otherwise, a no-op if already disconnected. UpdateNotificationState
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

        std::string label = std::string(DisplayName(*ev)) + "##we_live_report_" + ev->eventId;
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
                ImGui::SetTooltip(Tr("WE_LIVE_COOLDOWN_TOOLTIP_FMT"), remainingSec, DisplayName(*ev));
            }
            else
            {
                ImGui::SetTooltip(Tr("WE_LIVE_REPORT_TOOLTIP_FMT"), DisplayName(*ev));
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
        case WsConnectionState::Connected:  return Tr("WE_WSDEBUG_CONNECTED");
        case WsConnectionState::Connecting: return Tr("WE_WSDEBUG_CONNECTING");
        default:                            return Tr("WE_WSDEBUG_DISCONNECTED");
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
    std::string windowTitle = TrId("WE_LIVE_REPORTS_WINDOW_TITLE", kLiveEventReportsWindowId);

    bool isOpen = ImGui::Begin(windowTitle.c_str(), &ShowLiveEventReportsWindow, flags);

    //_ Deregistered instead while locked - NoInputs already makes the window click-through, so Escape shouldn't touch it either.
    if (LiveEventReportsWindowLocked)
        Localization_DeregisterCloseOnEscape(&ShowLiveEventReportsWindow);
    else
        Localization_SyncCloseOnEscape(&ShowLiveEventReportsWindow, windowTitle);

    if (!isOpen)
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(Tr("WE_LIVE_SHARD_LABEL_FMT"), ConnectionStateLabel(GetConnectionState()));

    ImGui::TextDisabled(Tr("WE_LIVE_REGION_LABEL_FMT"), ConnectionStateLabel(GetNotificationConnectionState()));
    std::optional<int> regionViewers = GetRegionViewerCount();
    if (regionViewers)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(Tr("WE_LIVE_ONLINE_COUNT_FMT"), *regionViewers,
            LiveEventsRegionToWireString(GetLiveEventsRegion()).c_str());
    }

    if (!MumbleLink)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_LIVE_NOT_IN_GAME"));
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

        std::string idLine = DisplayName(ev);
        if (octet)
            idLine += "." + std::to_string(*octet);

        std::vector<EventReport> reports = GetRecentReports(ev.eventId);
        if (reports.empty())
        {
            ImGui::TextUnformatted((idLine + " " + Tr("WE_LIVE_EMPTY_SUFFIX")).c_str());
            continue;
        }

        //_ Signed/clamped the same way subscriptions_notification.cpp treats its own tick-based elapsed time - a server-stamped ts should never be in the future, but a client clock can't be trusted not to disagree slightly.
        long long elapsedSigned = (long long)now - reports.front().timestampUnix; //. newest first, see GetRecentReports
        int elapsed = elapsedSigned > 0 ? (int)elapsedSigned : 0;
        char agoBuf[32];
        snprintf(agoBuf, sizeof(agoBuf), Tr("WE_LIVE_AGO_FMT"), FormatMinSec(elapsed).c_str());
        std::string treeLabel = idLine + " (" + agoBuf + ")";

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
                ImGui::BulletText(Tr("WE_LIVE_AGO_FMT"), FormatMinSec(e).c_str());
            }
            ImGui::TreePop();
        }
    }

    if (!any)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WE_LIVE_NO_EVENTS_ON_MAP"));
    }

    ImGui::End();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawColoredWrapped
//--------------------------------------------------------------------------------
// Wrapped text in one color, so a long line follows the window width instead of
// running past it.
//--------------------------------------------------------------------------------
static void DrawColoredWrapped(const ImVec4& color, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLink
//--------------------------------------------------------------------------------
// Wrapped text in kLinkColor with a hand cursor while hovered. Returns true on
// the frame it is clicked.
//--------------------------------------------------------------------------------
static bool DrawLink(const char* text)
{
    DrawColoredWrapped(kLinkColor, text);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribe
//--------------------------------------------------------------------------------
// The reporting switch, then whichever warning applies: no key (with a link to
// the key field) or a key whose region has not resolved yet. Neither blocks the
// switch - reporting needs no key (gw2_api.h). The last line links to the Help
// tab, which holds the feature's explainers.
//--------------------------------------------------------------------------------
static void DrawSubscribe()
{
    ImGui::Checkbox(Tr("WE_OPT_LIVE_SUBSCRIBE_CHECKBOX"), &LiveEventsSubscribed);
    Tooltip(Tr("WE_OPT_LIVE_SUBSCRIBE_TIP"));

    if (Gw2ApiKey.empty())
    {
        DrawColoredWrapped(kWarningColor, Tr("WE_OPT_LIVE_NO_API_KEY_WARNING"));
        if (DrawLink(Tr("WE_OPTWIN_LIVE_SET_API_KEY")))
        {
            OpenOptionsWindow(OptionsTab::General);
            RequestOpenAccountHeader();
        }
    }
    else if (GetLiveEventsRegion() == LiveEventsRegion::Unknown)
    {
        DrawColoredWrapped(kWarningColor, Tr("WE_OPT_LIVE_REGION_UNKNOWN_WARNING"));
    }

    if (DrawLink(Tr("WE_OPTWIN_LIVE_HELP_LINK")))
        OpenOptionsWindow(OptionsTab::Help);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawDisplay
//--------------------------------------------------------------------------------
// One setting per row. Lock window is indented under Show reports window and
// disabled while that is off. None of these depend on the switch above: the map
// locations and the Move button work unsubscribed.
//--------------------------------------------------------------------------------
static void DrawDisplay()
{
    ImGui::Checkbox(Tr("WE_OPT_LIVE_MOVE_BUTTON"), &LiveEventButtonMoveMode);
    Tooltip(Tr("WE_OPT_LIVE_MOVE_BUTTON_TIP"));

    ImGui::Checkbox(Tr("WE_OPT_LIVE_SHOW_REPORTS_WINDOW"), &ShowLiveEventReportsWindow);
    Tooltip(Tr("WE_OPT_LIVE_SHOW_REPORTS_WINDOW_TIP"));

    DisabledBlock(!ShowLiveEventReportsWindow)
    {
        float subToggleIndent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(subToggleIndent);
        ImGui::Checkbox(Tr("WE_OPT_LIVE_LOCK_WINDOW"), &LiveEventReportsWindowLocked);
        Tooltip(Tr("WE_OPT_LIVE_LOCK_WINDOW_TIP"));
        ImGui::Unindent(subToggleIndent);
    }

    ImGui::Checkbox(Tr("WE_OPT_LIVE_SHOW_MAP_DOTS"), &ShowLiveEventMapDots);
    Tooltip(Tr("WE_OPT_LIVE_SHOW_MAP_DOTS_TIP"));

    ImGui::Checkbox(Tr("WE_LIVE_SHARE_NAME_REPORTS"), &ShareNameInReports);
    Tooltip(Tr("WE_LIVE_SHARE_NAME_REPORTS_TIP"));

    //_ RGB only (feeds the toast's accent stripe via ToImVec4), no alpha.
    ImGui::ColorEdit3(TrId("WE_LIVE_REPORT_COLOR", "##sub_color_live").c_str(), SubscriptionsLiveColor, kSwatchFlags);
    Tooltip(Tr("WE_LIVE_REPORT_COLOR_TIP"));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawLiveEventRow
//--------------------------------------------------------------------------------
// Columns: subscribe, name (with its map id), "Only named", "Done today".
// Subscribing is itself the toast opt-in (subscriptions.h), so there is no
// notify-level ladder or tree to expand. The subscribe box is disabled while
// Gw2ApiKey is empty - region-wide toast delivery needs GetLiveEventsRegion
// (gw2_api.h). See IsLiveEventNamedOnly (subscriptions.h) for what "Only named"
// gates. isTarget is true for exactly one row, on the frame a deep link lands on
// it: the row starts the flash and is scrolled to.
//--------------------------------------------------------------------------------
static void DrawLiveEventRow(const LiveEvent& ev, bool isTarget)
{
    ImGui::TableNextRow();

    if (isTarget)
        OptionsHighlight_Set(ev.eventId);
    if (OptionsHighlight_IsActive(ev.eventId))
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    ImGui::TableSetColumnIndex(0);
    bool subscribed = IsLiveEventSubscribed(ev.eventId);
    DisabledBlock(Gw2ApiKey.empty())
    {
        if (DrawSubscribeCheckbox("##live_subscribe", subscribed))
            ToggleLiveEventSubscription(ev.eventId);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Gw2ApiKey.empty()
            ? Tr("WE_EDIT_LIVE_SUBSCRIBE_TIP_NO_KEY")
            : Tr("WE_EDIT_LIVE_SUBSCRIBE_TIP"));
    }

    if (isTarget)
        ImGui::SetScrollHereY(0.5f); //. after the row's first item, so it targets this row

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(DisplayName(ev));
    ImGui::SameLine();
    ImGui::TextDisabled("(map %d)", ev.mapId);

    ImGui::TableSetColumnIndex(2);
    bool namedOnly = IsLiveEventNamedOnly(ev.eventId);
    if (ImGui::Checkbox("##live_named_only", &namedOnly))
        ToggleLiveEventNamedOnly(ev.eventId);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_EDIT_LIVE_NAMED_ONLY_TIP"));

    ImGui::TableSetColumnIndex(3);
    bool doneToday = IsLiveEventMarkedDoneToday(ev.eventId);
    if (ImGui::Checkbox("##live_done", &doneToday))
        ToggleLiveEventDoneToday(ev.eventId);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Tr("WE_EDIT_LIVE_DONE_TIP"));
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawEventList
//--------------------------------------------------------------------------------
// One row per compiled-in LiveEvent under a one-line roster note; no search box,
// the roster is short. link is the deep link the tab was handed, if any; only a
// Live one names a row.
//--------------------------------------------------------------------------------
static void DrawEventList(const OptionsDeepLink* link)
{
    DrawColoredWrapped(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), Tr("WE_OPT_LIVE_ROSTER_NOTE"));

    if (g_LiveEvents.empty())
    {
        ImGui::TextDisabled("%s", Tr("WE_LIVE_NONE_COMPILED"));
        return;
    }

    if (!ImGui::BeginTable("##live_events", 4,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        return;

    ImGui::TableSetupColumn("##live_subscribe_col", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Tr("WE_EDIT_LIVE_COL_EVENT"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(Tr("WE_EDIT_LIVE_COL_ONLY_NAMED"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Tr("WE_EDIT_LIVE_COL_DONE_TODAY"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    const bool hasTarget = link && link->kind == SubscriptionKind::Live;
    for (const LiveEvent& ev : g_LiveEvents)
    {
        ImGui::PushID(ev.eventId.c_str());
        DrawLiveEventRow(ev, hasTarget && ev.eventId == link->liveEventId);
        ImGui::PopID();
    }

    ImGui::EndTable();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsLive   (see: options_live.h)
//--------------------------------------------------------------------------------
void DrawOptionsLive(const OptionsDeepLink* link)
{
    DrawSubscribe();
    ImGui::Spacing();
    DrawDisplay();
    ImGui::Spacing();
    DrawEventList(link);
}
