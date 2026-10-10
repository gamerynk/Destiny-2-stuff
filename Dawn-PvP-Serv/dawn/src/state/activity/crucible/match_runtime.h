#pragma once
#include "match_host.h"
namespace dawn::state::activity::crucible::runtime {
bool publication(std::uint64_t session, bool localReady, host::View& out) noexcept;
void observe_incident(std::uint64_t session, std::span<const std::byte> payload) noexcept;
bool for_account(std::uint64_t account, host::View& out) noexcept;
}
