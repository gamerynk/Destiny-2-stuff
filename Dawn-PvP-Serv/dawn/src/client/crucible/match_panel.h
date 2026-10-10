#pragma once
#include <cstdint>
#include "../../state/activity/crucible/match_host.h"
namespace dawn::client::crucible {
void activate() noexcept;
void service(std::uint64_t now) noexcept;
void shutdown() noexcept;
bool draw() noexcept;
bool try_snapshot(state::activity::crucible::host::View& view, std::uint64_t& receivedAt) noexcept;
}
