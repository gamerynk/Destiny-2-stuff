#pragma once

#include <cstddef>
#include <span>

#include "../../../state/entitlements/definition.h"

namespace dawn::middleware::signon::ownership {

inline constexpr std::size_t kEntryBytes = 6;
inline constexpr std::size_t kBufferSize = state::entitlements::kCapacity * kEntryBytes;

[[nodiscard]] bool encode(const state::entitlements::Table& entitlements,
                          std::span<std::byte> output,
                          std::size_t& size) noexcept;

} // namespace dawn::middleware::signon::ownership
