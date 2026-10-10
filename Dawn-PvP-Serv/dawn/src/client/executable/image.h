#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::client::executable {

inline constexpr std::size_t kPeSectionLimit = 96;

struct ExecutableImage {
    std::array<std::span<std::byte>, kPeSectionLimit> sections{};
    std::size_t count{};
};

[[nodiscard]] bool inspect(std::byte* base, ExecutableImage& output) noexcept;

[[nodiscard]] bool inspect_main_module(ExecutableImage& output) noexcept;

} // namespace dawn::client::executable
