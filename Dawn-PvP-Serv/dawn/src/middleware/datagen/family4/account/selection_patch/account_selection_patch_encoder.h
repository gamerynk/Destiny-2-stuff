#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::datagen::family4::account::selection_patch {

inline constexpr std::size_t kPayloadBitSize = 129;
inline constexpr std::size_t kPayloadSize = 17;

[[nodiscard]] bool
encode(std::uint64_t selectedSoid, std::span<std::byte> output, std::size_t& written) noexcept;

} // namespace dawn::middleware::datagen::family4::account::selection_patch
