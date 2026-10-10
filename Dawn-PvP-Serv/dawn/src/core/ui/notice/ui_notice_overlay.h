#pragma once

#include <cstddef>
#include <string_view>

namespace dawn::core::ui::notice {

inline constexpr std::size_t kTextCapacity = 128;

// never blocks, so a fault handler can call it; dropped when storage is busy
void raise(std::string_view text) noexcept;

void raise_information(std::string_view text) noexcept;

[[nodiscard]] bool draw() noexcept;

} // namespace dawn::core::ui::notice
