#include "ui_layout_navigation.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string_view>

#include "../../components/navigation/ui_navigation_component.h"
#include "../../modules/registry/ui_module_registry.h"
#include "../ui_layout_lifecycle.h"

namespace dawn::core::ui::layout::navigation {
namespace {

constexpr std::size_t kLabelTerminatorBytes = 1;
constexpr float kMinimumModuleListHeight = 1.0F;
constexpr float kAutomaticChildWidth = 0.0F;
constexpr ImGuiWindowFlags kModuleListWindowFlags = ImGuiWindowFlags_NoSavedSettings;

using modules::Descriptor;
using modules::registry::RegistrySnapshot;

[[nodiscard]] std::array<char, modules::kDisplayNameCapacity + kLabelTerminatorBytes>
widget_label(const Descriptor& descriptor) noexcept {
    std::array<char, modules::kDisplayNameCapacity + kLabelTerminatorBytes> label{};
    const std::string_view displayName = descriptor.display_name();
    std::copy(displayName.begin(), displayName.end(), label.begin());
    return label;
}

[[nodiscard]] Selection resolve_selection(const RegistrySnapshot& registrySnapshot,
                                          const StateSnapshot& state) noexcept {
    const auto entries = registrySnapshot.entries();
    if (entries.empty()) {
        internal::select_module({});
        return {};
    }

    const std::string_view selectedId(state.selectedStableId.data(), state.selectedStableIdLength);
    for (const Descriptor& descriptor : entries) {
        if (descriptor.stable_id() == selectedId) {
            return {descriptor, true};
        }
    }
    internal::select_module(entries.front().stable_id());
    return {entries.front(), true};
}

[[nodiscard]] Selection draw_module_choices(const RegistrySnapshot& registrySnapshot,
                                            Selection selected) noexcept {
    for (const Descriptor& descriptor : registrySnapshot.entries()) {
        const std::string_view stableId = descriptor.stable_id();
        ImGui::PushID(stableId.data(), stableId.data() + stableId.size());
        const auto label = widget_label(descriptor);
        if (components::navigation::tab(label.data(),
                                        selected.moduleAvailable
                                            && selected.descriptor.stable_id()
                                                   == descriptor.stable_id())) {
            internal::select_module(descriptor.stable_id());
            selected = {descriptor, true};
        }
        ImGui::PopID();
    }
    return selected;
}

} // namespace

Selection draw(const StateSnapshot& state) noexcept {
    ImGui::TextUnformatted("MODULES");
    ImGui::Separator();
    const RegistrySnapshot registrySnapshot = modules::registry::snapshot();
    Selection selected = resolve_selection(registrySnapshot, state);
    const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float listHeight =
        (std::max)(ImGui::GetContentRegionAvail().y - footerHeight, kMinimumModuleListHeight);
    if (ImGui::BeginChild("##module_list",
                          ImVec2(kAutomaticChildWidth, listHeight),
                          ImGuiChildFlags_None,
                          kModuleListWindowFlags)) {
        if (registrySnapshot.entries().empty()) {
            ImGui::TextDisabled("No modules registered.");
        } else {
            selected = draw_module_choices(registrySnapshot, selected);
        }
    }
    ImGui::EndChild();
    return selected;
}

} // namespace dawn::core::ui::layout::navigation
