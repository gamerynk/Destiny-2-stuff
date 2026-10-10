#include "ui_section_component.h"

#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "../label/ui_label_component.h"

namespace dawn::core::ui::components::section {
namespace {

constexpr float kRailWidth = 3.0F;
constexpr float kRailSpacing = label::kInset - kRailWidth;
constexpr float kRailVerticalInset = 2.0F;
constexpr float kRailRounding = 3.0F;
constexpr float kAutomaticWrapPosition = 0.0F;

} // namespace

void header(const char* title, const char* description) noexcept {
    if (title == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    const float railWidth = scaling::dpi::pixels(kRailWidth);
    const float railSpacing = scaling::dpi::pixels(kRailSpacing);
    ImGui::BeginGroup();
    ImGui::Dummy({railWidth, ImGui::GetTextLineHeight()});
    ImGui::SameLine(0.0F, railSpacing);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(title);
    if (description != nullptr && description[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(kAutomaticWrapPosition);
        ImGui::TextWrapped("%s", description);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::EndGroup();

    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const float railVerticalInset = scaling::dpi::pixels(kRailVerticalInset);
    ImGui::GetWindowDrawList()->AddRectFilled(
        {minimum.x, minimum.y + railVerticalInset},
        {minimum.x + railWidth, maximum.y - railVerticalInset},
        ImGui::GetColorU32(ImGuiCol_CheckMark),
        scaling::dpi::pixels(kRailRounding));
}

} // namespace dawn::core::ui::components::section
