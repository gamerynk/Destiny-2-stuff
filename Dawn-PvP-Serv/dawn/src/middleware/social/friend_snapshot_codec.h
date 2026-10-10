#pragma once

#include <cstddef>
#include <span>

#include "../../state/social/definition.h"

namespace dawn::middleware::social::friend_snapshot {

inline constexpr std::size_t kHeaderSize = 24 + state::social::kNameCapacity;
inline constexpr std::size_t kRecordSize = 416;
inline constexpr std::size_t kMaximumEncodedSize =
    kHeaderSize + state::social::kRemoteCapacity * kRecordSize;

[[nodiscard]] bool encode(const state::social::Snapshot& snapshot,
                          std::span<std::byte> output,
                          std::size_t& written) noexcept;

[[nodiscard]] bool decode(std::span<const std::byte> input,
                          state::social::Snapshot& snapshot) noexcept;

} // namespace dawn::middleware::social::friend_snapshot
