#include "ui_picker_component.h"

#include <array>
#include <cstring>
#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "../filter/ui_filter_component.h"

namespace dawn::core::ui::components::picker {
namespace {

constexpr char kFilterId[] = "filter";
constexpr char kFilterHint[] = "type to filter";
constexpr char kNoMatchLabel[] = "no match";
constexpr float kListHeight = 180.0F;

// ImGui keeps one popup open at a time, so one buffer serves every picker
std::array<char, kFilterCapacity> g_filter{};
// the combo popup uses a derived ID, so IsPopupOpen on the label never matches; testing
// it that way re-took keyboard focus every frame and left the rows unclickable
ImGuiID g_openPicker{};

[[nodiscard]] char lowercase(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool matches(const char* label, const char* needle) noexcept {
    if (needle[0] == '\0') {
        return true;
    }
    if (label == nullptr) {
        return false;
    }
    for (std::size_t start = 0; label[start] != '\0'; ++start) {
        std::size_t index = 0;
        while (needle[index] != '\0'
               && lowercase(label[start + index]) == lowercase(needle[index])) {
            ++index;
        }
        if (needle[index] == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

bool control(const char* id,
             const char* preview,
             std::span<const Item> items,
             std::size_t& selected) noexcept {
    if (id == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const ImGuiID pickerId = ImGui::GetID("##combo");
    bool picked = false;
    // the filter row plus fixed-height list would be clipped by the default height
    if (ImGui::BeginCombo(
            "##combo", preview != nullptr ? preview : "", ImGuiComboFlags_HeightLargest)) {
        if (g_openPicker != pickerId) {
            g_openPicker = pickerId;
            g_filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        (void)filter::input(kFilterId, kFilterHint, g_filter.data(), g_filter.size());
        ImGui::Separator();
        if (ImGui::BeginChild("##rows", {0.0F, scaling::dpi::pixels(kListHeight)})) {
            std::size_t shown = 0;
            for (std::size_t index = 0; index < items.size(); ++index) {
                const char* const label = items[index].label;
                if (!matches(label, g_filter.data())) {
                    continue;
                }
                ++shown;
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(label != nullptr ? label : "", index == selected)) {
                    selected = index;
                    picked = true;
                }
                ImGui::PopID();
            }
            if (shown == 0) {
                ImGui::TextUnformatted(kNoMatchLabel);
            }
        }
        ImGui::EndChild();
        if (picked) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    } else if (g_openPicker == pickerId) {
        g_openPicker = 0;
    }
    ImGui::PopID();
    return picked;
}

} // namespace dawn::core::ui::components::picker
