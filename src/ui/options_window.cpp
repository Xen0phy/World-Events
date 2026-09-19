//################################################################################
// options_window.cpp   (see: options_window.h)
//--------------------------------------------------------------------------------
// RailEntry                one rail button: tab, icon filename, tooltip id
// kRailTabs/kRailHelp      the rail's buttons; the icon filenames live here
// s_pendingLink            deep link waiting for the next drawn frame
// s_highlightId/Until      the one running row flash
// CurrentTab/SetCurrentTab OptionsWindowTab, clamped on read
// DrawRailButton           one icon-only button, tinted texture or dot
// DrawRail                 the three content tabs plus Help pinned below
//--------------------------------------------------------------------------------
// Rail icons are bundled textures resolved through GetOrRequestEventIcon
// (maprender.h), so they follow the same disk-first, bundled-fallback lookup as
// event icons and inherit its authoring rule: neutral gray RGB with the shape in
// alpha, recolored here by the same multiplicative tint. While a texture is still
// loading the button draws a plain dot instead, so the rail never shows an empty
// square.
//
// Every size derives from ImGui::GetFrameHeight(), never a fixed pixel value, so
// the window follows Nexus's UI scale. Each tab draws into its own child window
// ID, which gives every tab its own scroll position.
//--------------------------------------------------------------------------------

#include "options_window.h"

#include "addon_options_helpers.h" //. Tooltip
#include "imgui.h"
#include "localization.h"
#include "maprender.h" //. GetOrRequestEventIcon
#include "options_events.h"
#include "options_general.h"
#include "options_help.h"
#include "options_live.h"
#include "settings.h" //. OptionsWindowTab

#include <cfloat>
#include <string>

bool ShowOptionsWindow = false;

//_ Rail button side as a multiple of GetFrameHeight().
static constexpr float kRailButtonScale = 1.5f;

//_ Icon box as a fraction of the button side.
static constexpr float kRailIconFill = 0.7f;

//_ Length of the deep-link row flash, in seconds.
static constexpr double kHighlightDurationSec = 1.5;

//_ Initial window size in multiples of the font size, so it scales with the UI.
static constexpr float kInitialWidthEm  = 46.0f;
static constexpr float kInitialHeightEm = 36.0f;

//_ Smallest size the window can be resized to, same unit.
static constexpr float kMinWidthEm  = 28.0f;
static constexpr float kMinHeightEm = 20.0f;

//_ One child ID per tab, indexed by OptionsTab, so each tab keeps its own scroll position.
static const char* const kContentChildIds[kOptionsTabCount] = {
    "##options_content_general",
    "##options_content_events",
    "##options_content_live",
    "##options_content_help",
};

static OptionsDeepLink s_pendingLink;
static bool            s_hasPendingLink = false;

static std::string s_highlightId;
static double      s_highlightUntil = 0.0;

//********************************************************************************
// RailEntry
//--------------------------------------------------------------------------------
// tab         the OptionsTab this button selects
// iconFile    texture filename, resolved by GetOrRequestEventIcon
// tooltipId   localization identifier of the hover tooltip
//--------------------------------------------------------------------------------
struct RailEntry
{
    OptionsTab  tab;
    const char* iconFile;
    const char* tooltipId;
};

//_ Top-to-bottom order of the three content tabs; swapping an icon is a one-string edit here.
static constexpr RailEntry kRailTabs[] = {
    { OptionsTab::General, "BasicCross.png", "WE_OPTWIN_TAB_GENERAL" },
    { OptionsTab::Events,  "EventBoss.png",  "WE_OPTWIN_TAB_EVENTS"  },
    { OptionsTab::Live,    "Festival.png",   "WE_OPTWIN_TAB_LIVE"    },
};

//_ Pinned to the rail's bottom edge, apart from the content tabs.
static constexpr RailEntry kRailHelp = { OptionsTab::Help, "WorldBoss.png", "WE_OPTWIN_TAB_HELP" };

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CurrentTab / SetCurrentTab
//--------------------------------------------------------------------------------
// Read and write OptionsWindowTab. Reads clamp, so a stale or hand-edited
// settings.ini value falls back to General instead of indexing out of range.
//--------------------------------------------------------------------------------
static OptionsTab CurrentTab()
{
    int stored = OptionsWindowTab;
    if (stored < 0 || stored >= kOptionsTabCount)
        stored = (int)OptionsTab::General;
    return (OptionsTab)stored;
}

static void SetCurrentTab(OptionsTab tab)
{
    OptionsWindowTab = (int)tab;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawRailButton
//--------------------------------------------------------------------------------
// Manual hit-test and ImDrawList, the same approach as DrawNotifyLevelIcon
// (addon_options_helpers.cpp). Returns true on the frame the button is clicked.
// The icon keeps its aspect ratio inside a square box and is tinted with the text
// color, brighter while selected or hovered.
//--------------------------------------------------------------------------------
static bool DrawRailButton(const RailEntry& entry, bool selected, float side)
{
    ImGui::PushID((int)entry.tab);

    bool clicked = ImGui::InvisibleButton("##rail_button", ImVec2(side, side));
    bool hovered = ImGui::IsItemHovered();
    Tooltip(Tr(entry.tooltipId));

    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (selected)
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetStyle().FrameRounding);
    else if (hovered)
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetStyle().FrameRounding);

    ImU32 tint = ImGui::GetColorU32((selected || hovered) ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    ImVec2 center((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
    float boxHalf = side * kRailIconFill * 0.5f;

    Texture_t* icon = GetOrRequestEventIcon(entry.iconFile);
    if (icon && icon->Resource && icon->Width > 0 && icon->Height > 0)
    {
        float aspect = (float)icon->Width / (float)icon->Height;
        float halfW  = (aspect >= 1.0f) ? boxHalf : boxHalf * aspect;
        float halfH  = (aspect >= 1.0f) ? boxHalf / aspect : boxHalf;
        dl->AddImage((ImTextureID)icon->Resource,
            ImVec2(center.x - halfW, center.y - halfH),
            ImVec2(center.x + halfW, center.y + halfH),
            ImVec2(0, 0), ImVec2(1, 1), tint);
    }
    else
    {
        dl->AddCircleFilled(center, boxHalf * 0.4f, tint); //. texture still loading
    }

    ImGui::PopID();
    return clicked;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawRail
//--------------------------------------------------------------------------------
// Draws inside the rail child window. The Help button is placed by moving the
// cursor down by whatever height is left after the three content tabs.
//--------------------------------------------------------------------------------
static void DrawRail(float side)
{
    for (const RailEntry& entry : kRailTabs)
        if (DrawRailButton(entry, CurrentTab() == entry.tab, side))
            SetCurrentTab(entry.tab);

    float remaining = ImGui::GetContentRegionAvail().y;
    if (remaining > side)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remaining - side);

    if (DrawRailButton(kRailHelp, CurrentTab() == kRailHelp.tab, side))
        SetCurrentTab(kRailHelp.tab);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OpenOptionsWindow   (see: options_window.h)
//--------------------------------------------------------------------------------
void OpenOptionsWindow()
{
    ShowOptionsWindow = true;
}

void OpenOptionsWindow(OptionsTab tab)
{
    ShowOptionsWindow = true;
    SetCurrentTab(tab);
    s_hasPendingLink = false; //. explicit tab overrides any queued row
}

void OpenOptionsWindow(SubscriptionKind kind, const std::string& basicId,
    const CyclicSubscriptionKey& cyclicKey, const std::string& liveEventId)
{
    ShowOptionsWindow = true;
    SetCurrentTab(kind == SubscriptionKind::Live ? OptionsTab::Live : OptionsTab::Events);

    s_pendingLink = OptionsDeepLink{ kind, basicId, cyclicKey, liveEventId };
    s_hasPendingLink = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenderOptionsWindow   (see: options_window.h)
//--------------------------------------------------------------------------------
// Two child windows side by side: the fixed-width rail (bordered, so it reads as
// a separate strip) and the content pane filling the rest. The deep link is
// copied out of the pending slot before drawing, so it reaches the section for
// exactly this one frame. Esc-to-close is handled by Nexus via the registration
// Localization_SyncCloseOnEscape keeps in step with the translated title.
//--------------------------------------------------------------------------------
void RenderOptionsWindow()
{
    if (!ShowOptionsWindow) return;

    float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fontSize * kInitialWidthEm, fontSize * kInitialHeightEm), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(fontSize * kMinWidthEm, fontSize * kMinHeightEm), ImVec2(FLT_MAX, FLT_MAX));

    //_ Force-uncollapses so a pending deep-link target is actually visible.
    if (s_hasPendingLink)
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    std::string windowTitle = TrId("WE_OPTWIN_TITLE", kOptionsWindowId);
    bool isOpen = ImGui::Begin(windowTitle.c_str(), &ShowOptionsWindow);
    Localization_SyncCloseOnEscape(&ShowOptionsWindow, windowTitle);

    if (!isOpen)
    {
        //_ Collapsed, not closed - still balance Begin() with End().
        ImGui::End();
        return;
    }

    OptionsDeepLink link;
    bool hasLink = s_hasPendingLink;
    if (hasLink)
    {
        link = s_pendingLink;
        s_hasPendingLink = false;
    }
    const OptionsDeepLink* linkPtr = hasLink ? &link : nullptr;

    float side = ImGui::GetFrameHeight() * kRailButtonScale;
    float railWidth = side + ImGui::GetStyle().WindowPadding.x * 2.0f;

    ImGui::BeginChild("##options_rail", ImVec2(railWidth, 0.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawRail(side);
    ImGui::EndChild();

    ImGui::SameLine();

    //_ Read after the rail ran, so a click switches the content the same frame.
    OptionsTab tab = CurrentTab();

    ImGui::BeginChild(kContentChildIds[(int)tab], ImVec2(0.0f, 0.0f), false);
    switch (tab)
    {
        case OptionsTab::General: DrawOptionsGeneral();         break;
        case OptionsTab::Events:  DrawOptionsEvents(linkPtr);   break;
        case OptionsTab::Live:    DrawOptionsLive(linkPtr);     break;
        case OptionsTab::Help:    DrawOptionsHelp();            break;
    }
    ImGui::EndChild();

    ImGui::End();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// OptionsHighlight_Set / OptionsHighlight_IsActive   (see: options_window.h)
//--------------------------------------------------------------------------------
// Timed with ImGui::GetTime(), so no platform header is needed here.
//--------------------------------------------------------------------------------
void OptionsHighlight_Set(const std::string& id)
{
    s_highlightId    = id;
    s_highlightUntil = ImGui::GetTime() + kHighlightDurationSec;
}

bool OptionsHighlight_IsActive(const std::string& id)
{
    return !s_highlightId.empty() && id == s_highlightId && ImGui::GetTime() < s_highlightUntil;
}
