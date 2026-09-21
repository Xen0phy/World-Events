//################################################################################
// options_widgets.cpp   (see: options_widgets.h)
//--------------------------------------------------------------------------------

#include "options_widgets.h"

#include "imgui.h"
#include "imgui_internal.h" //. GImGui, PushItemFlag/PopItemFlag, ImGuiItemFlags_Disabled
#include "localization.h" //. Tr, for the empty entry of DrawFileCombo

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ImGuiScopedDisabled ctor / dtor   (see: options_widgets.h)
//--------------------------------------------------------------------------------
ImGuiScopedDisabled::ImGuiScopedDisabled(bool cond) : active(cond)
{
    if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
}

ImGuiScopedDisabled::~ImGuiScopedDisabled()
{
    if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tooltip
//--------------------------------------------------------------------------------
// Reads GImGui->HoveredIdTimer from imgui_internal.h. That is an ImGui internal,
// but the standard hover-delay approach before ImGui's own hover-delay API landed
// in later versions.
//--------------------------------------------------------------------------------
void Tooltip(const char* text, float delaySeconds)
{
    if (ImGui::IsItemHovered())
    {
        if (GImGui->HoveredIdTimer >= delaySeconds)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", text);
            ImGui::EndTooltip();
        }
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawSubscribeCheckbox
//--------------------------------------------------------------------------------
// A plain ImGui::Checkbox is noticeably taller than a TreeNode arrow, so
// FramePadding is zeroed for this one call.
//--------------------------------------------------------------------------------
bool DrawSubscribeCheckbox(const char* label, bool& value)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleVar();
    return changed;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawFileCombo   (see: options_widgets.h)
//--------------------------------------------------------------------------------
void DrawFileCombo(const char* label, const char* emptyKey, const std::vector<std::string>& files, std::string& value)
{
    std::vector<const char*> labels;
    labels.push_back(Tr(emptyKey));
    for (const std::string& name : files)
        labels.push_back(name.c_str());

    int index = 0;
    for (int i = 0; i < (int)files.size(); i++)
    {
        if (files[i] == value)
        {
            index = i + 1;
            break;
        }
    }

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo(label, &index, labels.data(), (int)labels.size()))
        value = (index == 0) ? std::string() : files[index - 1];
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawBellIcon / DrawSpeakerIcon
//--------------------------------------------------------------------------------
// Drawn with ImDrawList primitives, both authored in the same 24-unit box, so the
// scale factor is size/24.
//--------------------------------------------------------------------------------
void DrawBellIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; //. 24-unit authoring box
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    //_ Dome + flare: a semicircle over the top, then straight lines flaring to the rim; filled solid, not stroked.
    dl->PathArcTo(P(12.0f, 14.0f), 6.0f * s, IM_PI, IM_PI * 2.0f, 12);
    dl->PathLineTo(P(20.0f, 18.0f));
    dl->PathLineTo(P(4.0f, 18.0f));
    dl->PathFillConvex(color);

    dl->AddCircleFilled(P(12.0f, 20.4f), 1.3f * s, color, 12); //. clapper, not just a dome
}

void DrawSpeakerIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color)
{
    float s = size / 24.0f; //. same box as DrawBellIcon
    ImVec2 origin(center.x - 12.0f * s, center.y - 12.0f * s);
    auto P = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };

    dl->AddRectFilled(P(5.0f, 9.0f), P(11.5f, 15.0f), color); //. housing, overlaps cone

    //_ Cone/flare drawn as its own convex trapezoid since the combined housing+cone silhouette isn't convex.
    dl->PathLineTo(P(11.0f, 9.0f));
    dl->PathLineTo(P(16.0f, 4.0f));
    dl->PathLineTo(P(16.0f, 20.0f));
    dl->PathLineTo(P(11.0f, 15.0f));
    dl->PathFillConvex(color);

    //_ Sound waves: two concentric arcs, stroked; a filled crescent this small would look like a smudge, not a wave.
    dl->PathArcTo(P(11.0f, 12.0f), 5.0f * s, -0.65f, 0.65f, 8);
    dl->PathStroke(color, false, 1.4f * s);

    dl->PathArcTo(P(11.0f, 12.0f), 8.5f * s, -0.55f, 0.55f, 8);
    dl->PathStroke(color, false, 1.4f * s);
}
