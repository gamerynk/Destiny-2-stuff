#include "ui_card_component.h"

#include <algorithm>
#include <imgui.h>

#include "../../animation/transition/ui_transition_animation.h"
#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "../drawing/drawing.h"

namespace dawn::core::ui::components::card {
namespace {

constexpr float kMinimumExtent = 1.0F;
constexpr float kCornerRadius = 9.0F;
constexpr ImVec2 kContentPadding{12.0F, 10.0F};
constexpr float kHoverFillWeight = 0.25F;
constexpr float kHoverBorderWeight = 0.32F;
constexpr float kBorderThickness = 1.0F;
constexpr int kScopedStyleVariableCount = 3;
constexpr animation::transition::Rates kHoverRates{9.0F, 7.0F};
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoSavedSettings;
constexpr ImGuiChildFlags kChildFlags = ImGuiChildFlags_AlwaysUseWindowPadding;

[[nodiscard]] ImVec2 resolve_size(const ImVec2& requested) noexcept {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float minimumExtent = scaling::dpi::pixels(kMinimumExtent);
    return {(std::max)(requested.x > 0.0F ? requested.x : available.x, minimumExtent),
            (std::max)(requested.y > 0.0F ? requested.y : available.y, minimumExtent)};
}

} // namespace

Scope::Scope(const char* id, const ImVec2& size) noexcept {
    if (id == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    const ImVec2 resolvedSize = resolve_size(size);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum{minimum.x + resolvedSize.x, minimum.y + resolvedSize.y};
    const ImGuiID cardId = ImGui::GetID(id);
    const bool hovered = ImGui::IsMouseHoveringRect(minimum, maximum, true);
    const float hover = animation::transition::update(
        cardId, animation::transition::Lane::hover, hovered, kHoverRates, 0.0F);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 fill = drawing::blend(style.Colors[ImGuiCol_ChildBg],
                                       style.Colors[ImGuiCol_FrameBgHovered],
                                       hover * kHoverFillWeight);
    const ImVec4 border = drawing::blend(style.Colors[ImGuiCol_Border],
                                         style.Colors[ImGuiCol_CheckMark],
                                         hover * kHoverBorderWeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cornerRadius = scaling::dpi::pixels(kCornerRadius);
    const float borderThickness = scaling::dpi::pixels(kBorderThickness);
    drawList->AddRectFilled(minimum, maximum, ImGui::GetColorU32(fill), cornerRadius);
    drawList->AddRect(minimum,
                      maximum,
                      ImGui::GetColorU32(border),
                      cornerRadius,
                      ImDrawFlags_None,
                      borderThickness);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, cornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scaling::dpi::pixels(kContentPadding));
    visible_ = ImGui::BeginChild(id, resolvedSize, kChildFlags, kWindowFlags);
    started_ = true;
    ImGui::PopStyleVar(kScopedStyleVariableCount);
    ImGui::PopStyleColor();
}

Scope::~Scope() noexcept {
    if (started_) {
        ImGui::EndChild();
    }
}

bool Scope::visible() const noexcept {
    return visible_;
}

} // namespace dawn::core::ui::components::card
