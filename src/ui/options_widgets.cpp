//################################################################################
// options_widgets.cpp   (see: options_widgets.h)
//--------------------------------------------------------------------------------

#include "options_widgets.h"

#include "imgui.h"
#include "imgui_internal.h" //. GImGui, ImGuiWindow, ImMin, IM_FLOOR, item flags
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
// ImGuiScopedGroupBox ctor / dtor   (see: options_widgets.h)
//--------------------------------------------------------------------------------
// The top border runs through the middle of the title row; content starts one
// pad.y below the row, indented by pad.x. The title is clipped at titleEndX. The
// height is unknown until the content is drawn, so the destructor draws the
// border as one open path that skips the gap under the title, with AddRect's
// half-pixel alignment. No draw list channel split, since tables already use one.
// The box's own item is a Dummy spanning the whole frame, added last.
//--------------------------------------------------------------------------------
ImGuiScopedGroupBox::ImGuiScopedGroupBox(const char* title, float width)
{
    ImGui::PushID(title);

    const ImGuiStyle& style = ImGui::GetStyle();
    const char* titleEnd = ImGui::FindRenderedTextEnd(title);
    const ImVec2 textSize = ImGui::CalcTextSize(title, titleEnd);
    const float gap = style.ItemInnerSpacing.x;

    pad = style.WindowPadding;
    origin = ImGui::GetCursorScreenPos();
    right = origin.x + ((width > 0.0f) ? width : ImGui::GetContentRegionAvail().x + width);
    titleHeight = textSize.y;
    const float inset = ImMax(pad.x, style.ChildRounding);
    titleStartX = origin.x + inset;
    titleEndX = ImMin(titleStartX + gap + textSize.x + gap, right - inset);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    regionMaxX = window->ContentRegionRect.Max.x;
    workMaxX = window->WorkRect.Max.x;
    window->ContentRegionRect.Max.x = window->WorkRect.Max.x = right - pad.x;

    ImGui::BeginGroup();
    const float textX = titleStartX + gap;
    const ImVec4 clip(textX, origin.y, titleEndX, origin.y + titleHeight);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, origin.y), ImGui::GetColorU32(ImGuiCol_Text), title, titleEnd, 0.0f, &clip);

    ImGui::Indent(pad.x);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad.x, origin.y + titleHeight + pad.y));
}

ImGuiScopedGroupBox::~ImGuiScopedGroupBox()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    window->ContentRegionRect.Max.x = regionMaxX;
    window->WorkRect.Max.x = workMaxX;

    ImGui::EndGroup();
    const float bottom = ImGui::GetItemRectMax().y + pad.y;

    const float l = IM_FLOOR(origin.x) + 0.5f;
    const float r = IM_FLOOR(right) - 0.5f;
    const float t = IM_FLOOR(origin.y + titleHeight * 0.5f) + 0.5f;
    const float b = IM_FLOOR(bottom) - 0.5f;
    const float round = ImMin(ImGui::GetStyle().ChildRounding, (b - t) * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PathLineTo(ImVec2(titleEndX, t));
    dl->PathArcToFast(ImVec2(r - round, t + round), round, 9, 12);
    dl->PathArcToFast(ImVec2(r - round, b - round), round, 0, 3);
    dl->PathArcToFast(ImVec2(l + round, b - round), round, 3, 6);
    dl->PathArcToFast(ImVec2(l + round, t + round), round, 6, 9);
    dl->PathLineTo(ImVec2(titleStartX, t));
    dl->PathStroke(ImGui::GetColorU32(ImGuiCol_Border), false, 1.0f);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(right - origin.x, bottom - origin.y));
    ImGui::PopID();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SeparatorText
//--------------------------------------------------------------------------------
// The lines sit at the vertical middle of the text row, with AddLine's half-pixel
// alignment. The row is one Dummy as wide as the content region and one
// ItemSpacing.y taller than the text, which puts the label that much below the
// previous item. The label is clipped at the right edge, and the right line is
// dropped when no room is left for it.
//--------------------------------------------------------------------------------
void SeparatorText(const char* label)
{
    if (ImGui::GetCurrentWindow()->SkipItems)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const float textHeight = ImGui::GetTextLineHeight();
    const float top = style.ItemSpacing.y;
    const float gap = style.ItemInnerSpacing.x;
    const float stub = style.WindowPadding.x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width, top + textHeight));

    const float right = pos.x + width;
    const float y = IM_FLOOR(pos.y + top + textHeight * 0.5f) + 0.5f;
    const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_Separator);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (labelEnd == label)
    {
        dl->AddLine(ImVec2(pos.x, y), ImVec2(right, y), lineColor);
        return;
    }

    const float textX = pos.x + stub + gap;
    const float textEndX = ImMin(textX + ImGui::CalcTextSize(label, labelEnd).x, right);
    const ImVec4 clip(textX, pos.y, right, pos.y + top + textHeight);
    dl->AddLine(ImVec2(pos.x, y), ImVec2(ImMin(pos.x + stub, right), y), lineColor);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, pos.y + top), ImGui::GetColorU32(ImGuiCol_Text), label, labelEnd, 0.0f, &clip);
    if (textEndX + gap < right)
        dl->AddLine(ImVec2(textEndX + gap, y), ImVec2(right, y), lineColor);
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