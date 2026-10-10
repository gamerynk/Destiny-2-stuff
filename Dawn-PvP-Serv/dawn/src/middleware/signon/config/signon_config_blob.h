#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../../../state/runtime/state.h"

namespace dawn::middleware::signon::config {

inline constexpr std::size_t kBlobSize = 113 + state::kBootstrapTokenSize;
inline constexpr std::size_t kBlobCapacity = kBlobSize + 2 * (4 + 1 + 127) + 4 + 1 + 36 + 5;

[[nodiscard]] bool build(std::span<const std::byte> token, std::span<std::byte> output) noexcept;
[[nodiscard]] bool build(std::span<const std::byte> token, std::string_view url,
                         std::string_view guid, std::span<std::byte> output,
                         std::size_t& written) noexcept;

} // namespace dawn::middleware::signon::config
