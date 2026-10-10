#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::matchmaking {

[[nodiscard]] bool acquire_context(ContextHandle& context) noexcept;

[[nodiscard]] bool release_context(ContextHandle context) noexcept;

[[nodiscard]] bool prepare_variant_update(ContextHandle context,
                                          std::uint64_t requestId,
                                          std::uint64_t variant,
                                          bool hasDescriptor,
                                          std::span<const std::byte> descriptor,
                                          std::uint64_t& advertisementId,
                                          PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_initial_latest(ContextHandle context,
                                          std::uint64_t& advertisementId,
                                          PendingMutation& mutation) noexcept;

[[nodiscard]] bool latest_snapshot(ContextHandle context, LatestSnapshot& snapshot) noexcept;

[[nodiscard]] bool commit(PendingMutation& mutation) noexcept;

void erase_snapshot(LatestSnapshot& snapshot) noexcept;

} // namespace dawn::state::matchmaking
