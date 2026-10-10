#pragma once

#include <cstdint>

#include "definition.h"

namespace dawn::state { struct AccountRuntimeContext; }

namespace dawn::state::activity::destination {

[[nodiscard]] bool snapshot(std::uint64_t sessionId, DestinationSelection& output) noexcept;

[[nodiscard]] bool snapshot_for_runtime(const AccountRuntimeContext& context,
                                       std::uint64_t authenticatedAccountSoid,
                                       std::uint64_t sessionId,
                                       DestinationSelection& output) noexcept;

} // namespace dawn::state::activity::destination
