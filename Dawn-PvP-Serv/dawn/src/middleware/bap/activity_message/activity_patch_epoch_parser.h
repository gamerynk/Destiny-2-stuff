#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::patch_epoch {

inline constexpr std::uint32_t kMessageType = 52;
inline constexpr std::size_t kEncodedSize = 16;

struct PatchEpoch final {
    std::uint64_t first{};
    std::uint64_t second{};
};

[[nodiscard]] bool parse_patch_epoch(std::span<const std::byte> input, PatchEpoch& epoch) noexcept;

} // namespace dawn::middleware::bap::activity_message::patch_epoch
