#pragma once
#include "runtime.h"

namespace dawn::state::activity::shared {
[[nodiscard]] bool social_destination(const destination::DestinationSelection& destination) noexcept;
[[nodiscard]] bool prepare_social_allocation(PendingAllocation& plan) noexcept;
[[nodiscard]] bool commit_social_allocation(const PendingAllocation& plan) noexcept;
[[nodiscard]] bool social_allocation_valid_locked(const PendingAllocation& plan) noexcept;
[[nodiscard]] bool social_join_authorized(std::uint64_t session,std::uint64_t key,
                                         std::uint64_t character) noexcept;
[[nodiscard]] bool social_session(std::uint64_t session) noexcept;
}
