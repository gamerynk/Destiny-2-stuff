#include "ui_navigation_component.h"

#include <algorithm>
#include <imgui.h>

#include "../../animation/transition/ui_transition_animation.h"
#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "../drawing/drawing.h"

namespace dawn::core::ui::components::navigation {
namespace {

constexpr float kDefaultHeight = 34.0F;
constexpr float kMinimumWidth = 1.0F;
constexpr float kMinimumHeight = 16.0F;
constexpr float kCornerRadius = 7.0F;
constexpr float kLabelInset = 10.0F;
constexpr float kSelectedTextShift = 4.0F;
constexpr float kHoveredTextShift = 2.0F;
constexpr float kSelectionRailWidth = 3.0F;
constexpr float kSelectionRailInset = 7.0F;
constexpr float kHoverFillOpacity = 0.45F;
constexpr float kSelectionFillOpacity = 0.72F;
constexpr float kBorderThickness = 1.0F;
constexpr animation::transition::Rates kHoverRates{18.0F, 12.0F};
constexpr animation::transition::Rates kSelectionRates{13.0F, 10.0F};

} // namespace

bool tab(const char* label,
         bool selected,
         float width,
         float height,
         LabelAlignment alignment) noexcept {
    if (label == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float minimumWidth = scaling::dpi::pixels(kMinimumWidth);
    const float minimumHeight = scaling::dpi::pixels(kMinimumHeight);
    const float defaultHeight = scaling::dpi::pixels(kDefaultHeight);
    const float resolvedWidth =
        (std::max)(width > 0.0F ? width : ImGui::GetContentRegionAvail().x, minimumWidth);
    const float resolvedHeight = (std::max)(height > 0.0F ? height : defaultHeight, minimumHeight);
    const ImVec2 size{resolvedWidth, resolvedHeight};
    const ImGuiID id = ImGui::GetID(label);
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();

    const float hover = animation::transition::update(
        id, animation::transition::Lane::hover, hovered, kHoverRates, 0.0F);
    const float selection = animation::transition::update(id,
                                                          animation::transition::Lane::selection,
                                                          selected,
                                                          kSelectionRates,
                                                          selected ? 1.0F : 0.0F);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fillWeight =
        (std::max)(hover * kHoverFillOpacity, selection * kSelectionFillOpacity);
    ImVec4 fill = drawing::blend(
        style.Colors[ImGuiCol_ChildBg], style.Colors[ImGuiCol_HeaderHovered], fillWeight);
    fill = drawing::blend(fill, style.Colors[ImGuiCol_Header], selection);
    const ImVec4 border =
        drawing::blend(style.Colors[ImGuiCol_Border], style.Colors[ImGuiCol_CheckMark], selection);
    const ImVec4 text = drawing::blend(style.Colors[ImGuiCol_TextDisabled],
                                       style.Colors[ImGuiCol_Text],
                                       (std::max)(hover, selection));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 maximum{position.x + size.x, position.y + size.y};
    const float cornerRadius = scaling::dpi::pixels(kCornerRadius);
    const float borderThickness = scaling::dpi::pixels(kBorderThickness);
    drawList->AddRectFilled(position, maximum, ImGui::GetColorU32(fill), cornerRadius);
    drawList->AddRect(position,
                      maximum,
                      ImGui::GetColorU32(border),
                      cornerRadius,
                      ImDrawFlags_None,
                      borderThickness);

    const float railStrength = (std::max)(hover * kHoverFillOpacity, selection);
    const float railWidth = scaling::dpi::pixels(kSelectionRailWidth);
    const float railInset = scaling::dpi::pixels(kSelectionRailInset);
    ImVec4 rail = style.Colors[ImGuiCol_CheckMark];
    rail.w *= railStrength;
    drawList->AddRectFilled({position.x, position.y + railInset},
                            {position.x + railWidth, maximum.y - railInset},
                            ImGui::GetColorU32(rail),
                            railWidth);

    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    const float textShift = alignment == LabelAlignment::leading
                                ? (selection * scaling::dpi::pixels(kSelectedTextShift))
                                      + (hover * scaling::dpi::pixels(kHoveredTextShift))
                                : 0.0F;
    const float textX = alignment == LabelAlignment::centered
                            ? position.x + ((size.x - textSize.x) * 0.5F)
                            : position.x + scaling::dpi::pixels(kLabelInset) + textShift;
    const ImVec2 textPosition{textX, position.y + ((size.y - textSize.y) * 0.5F)};
    drawList->AddText(
        textPosition, ImGui::GetColorU32(text), label, drawing::visible_text_end(label));
    return pressed;
}

} // namespace dawn::core::ui::components::navigation
