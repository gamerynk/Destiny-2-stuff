#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "../../../patterns/registry.h"

namespace dawn::client::targets::game::resolution {

enum class Failure : std::uint8_t {
    none,
    signature,
    networkDerive,
    contentDerive,
};

[[nodiscard]] bool resolve(std::span<const patterns::ImageRange> image) noexcept;

[[nodiscard]] Failure last_failure() noexcept;

[[nodiscard]] std::string_view last_failed_signature() noexcept;

} // namespace dawn::client::targets::game::resolution
