#include "ui_toggle_component.h"

#include <algorithm>
#include <imgui.h>

#include "../../animation/transition/ui_transition_animation.h"
#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "../drawing/drawing.h"
#include "../label/ui_label_component.h"

namespace dawn::core::ui::components::toggle {
namespace {

constexpr float kControlHeight = 24.0F;
constexpr float kTrackWidth = 40.0F;
constexpr float kMinimumWidth = kTrackWidth;
constexpr float kTrackHeight = 20.0F;
constexpr float kThumbInset = 3.0F;
constexpr float kThumbRadius = 7.0F;
constexpr float kRowRounding = 8.0F;
constexpr float kLabelTrackSpacing = 8.0F;
constexpr float kTrackBorderThickness = 1.0F;
constexpr float kRowHoverOpacity = 0.22F;
constexpr float kTrackHoverWeight = 0.34F;
constexpr animation::transition::Rates kSelectionRates{16.0F, 13.0F};
constexpr animation::transition::Rates kHoverRates{20.0F, 15.0F};
constexpr animation::transition::Rates kFocusRates{24.0F, 20.0F};

} // namespace

bool control(const char* label, bool& value, float width) noexcept {
    if (label == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float controlHeight = scaling::dpi::pixels(kControlHeight);
    const float trackWidth = scaling::dpi::pixels(kTrackWidth);
    const float trackHeight = scaling::dpi::pixels(kTrackHeight);
    const float thumbInset = scaling::dpi::pixels(kThumbInset);
    const float thumbRadius = scaling::dpi::pixels(kThumbRadius);
    const float minimumWidth = scaling::dpi::pixels(kMinimumWidth);
    const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, minimumWidth);
    const float resolvedWidth =
        width > 0.0F ? (std::clamp)(width, minimumWidth, availableWidth) : availableWidth;
    const ImGuiID id = ImGui::GetID(label);
    const bool changed = ImGui::InvisibleButton(label, {resolvedWidth, controlHeight});
    if (changed) {
        value = !value;
    }

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const float hover = animation::transition::update(
        id, animation::transition::Lane::hover, hovered, kHoverRates, 0.0F);
    const float selection = animation::transition::update(
        id, animation::transition::Lane::selection, value, kSelectionRates, value ? 1.0F : 0.0F);
    const float focus = animation::transition::update(
        id, animation::transition::Lane::focus, active, kFocusRates, 0.0F);

    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 rowFill = style.Colors[ImGuiCol_HeaderHovered];
    rowFill.w *= hover * kRowHoverOpacity;
    drawList->AddRectFilled(position,
                            {position.x + resolvedWidth, position.y + controlHeight},
                            ImGui::GetColorU32(rowFill),
                            scaling::dpi::pixels(kRowRounding));

    const float trackX = position.x + resolvedWidth - trackWidth;
    const float trackY = position.y + ((controlHeight - trackHeight) * 0.5F);
    const ImVec2 trackMinimum{trackX, trackY};
    const ImVec2 trackMaximum{trackX + trackWidth, trackY + trackHeight};
    const ImVec4 hoveredTrack = drawing::blend(style.Colors[ImGuiCol_FrameBg],
                                               style.Colors[ImGuiCol_FrameBgHovered],
                                               hover * kTrackHoverWeight);
    const ImVec4 track = drawing::blend(hoveredTrack, style.Colors[ImGuiCol_CheckMark], selection);
    const ImVec4 trackBorder = drawing::blend(style.Colors[ImGuiCol_Border],
                                              style.Colors[ImGuiCol_CheckMark],
                                              (std::max)(selection, focus));
    drawList->AddRectFilled(
        trackMinimum, trackMaximum, ImGui::GetColorU32(track), trackHeight * 0.5F);
    drawList->AddRect(trackMinimum,
                      trackMaximum,
                      ImGui::GetColorU32(trackBorder),
                      trackHeight * 0.5F,
                      ImDrawFlags_None,
                      scaling::dpi::pixels(kTrackBorderThickness));

    const float thumbTravel = trackWidth - (thumbInset * 2.0F) - (thumbRadius * 2.0F);
    const float thumbX = trackX + thumbInset + thumbRadius + (thumbTravel * selection);
    const float thumbY = trackY + (trackHeight * 0.5F);
    drawList->AddCircleFilled(
        {thumbX, thumbY}, thumbRadius, ImGui::GetColorU32(style.Colors[ImGuiCol_Text]));

    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    // qualified from the parent namespace because the label parameter hides its name
    const ImVec2 textPosition{position.x + components::label::inset(),
                              position.y + ((controlHeight - textSize.y) * 0.5F)};
    const float textMaximumX = trackX - scaling::dpi::pixels(kLabelTrackSpacing);
    if (textMaximumX > textPosition.x) {
        drawList->PushClipRect(textPosition, {textMaximumX, position.y + controlHeight}, true);
        drawList->AddText(textPosition,
                          ImGui::GetColorU32(style.Colors[ImGuiCol_Text]),
                          label,
                          drawing::visible_text_end(label));
        drawList->PopClipRect();
    }
    return changed;
}

} // namespace dawn::core::ui::components::toggle
