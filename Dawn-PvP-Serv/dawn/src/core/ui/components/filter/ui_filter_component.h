#pragma once

#include <cstddef>
#include <imgui.h>

namespace dawn::core::ui::components::filter {

[[nodiscard]] bool input(const char* id,
                         const char* hint,
                         char* buffer,
                         std::size_t capacity,
                         ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) noexcept;

} // namespace dawn::core::ui::components::filter
