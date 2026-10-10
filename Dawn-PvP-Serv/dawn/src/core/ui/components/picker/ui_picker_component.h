#pragma once

#include <cstddef>
#include <span>

namespace dawn::core::ui::components::picker {

inline constexpr std::size_t kFilterCapacity = 64;

struct Item {
    const char* label{};
};

[[nodiscard]] bool control(const char* id,
                           const char* preview,
                           std::span<const Item> items,
                           std::size_t& selected) noexcept;

} // namespace dawn::core::ui::components::picker
