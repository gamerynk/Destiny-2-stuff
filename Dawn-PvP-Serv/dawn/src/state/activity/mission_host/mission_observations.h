#pragma once

#include <cstdint>

#include "../../../middleware/mission/observation_wire.h"

namespace dawn::state::activity::mission_observations {

enum class Result : std::uint8_t {
    applied,
    notMember,
};

[[nodiscard]] Result apply(std::uint64_t platformId,
                           const middleware::mission::observation_wire::Request& request,
                           middleware::mission::observation_wire::Response& response) noexcept;

} // namespace dawn::state::activity::mission_observations
