#pragma once

#include <cstdint>
#include <imgui.h>

namespace dawn::core::ui::textures {

enum class Slot : std::uint8_t {
    logoSheet,
    count,
};

void publish(Slot slot, ImTextureID texture) noexcept;

[[nodiscard]] ImTextureID get(Slot slot) noexcept;

void clear() noexcept;

} // namespace dawn::core::ui::textures
