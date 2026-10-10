#pragma once

#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"

namespace dawn::core::ui::components::label {

constexpr float kInset = 10.0F;

[[nodiscard]] inline float inset() noexcept {
    return scaling::dpi::pixels(kInset);
}

inline void align() noexcept {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset());
}

} // namespace dawn::core::ui::components::label
