#pragma once

#include <array>
#include <cstddef>

#include "../modules/ui_module_descriptor.h"

namespace dawn::core::ui::layout {

struct StateSnapshot {
    bool initialized{};
    std::array<char, modules::kStableIdCapacity> selectedStableId{};
    std::size_t selectedStableIdLength{};
};

[[nodiscard]] bool initialize() noexcept;

[[nodiscard]] bool render(bool visible) noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] StateSnapshot snapshot() noexcept;

} // namespace dawn::core::ui::layout
