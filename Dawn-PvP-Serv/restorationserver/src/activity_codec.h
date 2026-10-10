#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "state/activity/definition.h"

namespace restoration::activity_codec {

inline constexpr int kVersion = 1;

[[nodiscard]] bool encode(const dawn::state::activity::ActivityState& activity,
                          std::vector<std::byte>& output) noexcept;

[[nodiscard]] bool decode(std::span<const std::byte> input,
                          std::uint64_t ownerAccountSoid,
                          dawn::state::activity::ActivityState& activity) noexcept;

[[nodiscard]] bool self_test(const dawn::state::activity::ActivityState& activity,
                             std::uint64_t ownerAccountSoid) noexcept;

} // namespace restoration::activity_codec
