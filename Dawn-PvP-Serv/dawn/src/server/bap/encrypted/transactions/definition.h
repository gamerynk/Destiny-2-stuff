#pragma once

#include <cstdint>

namespace dawn::server::bap::encrypted::transactions {

struct Publication {
    std::uint64_t activitySessionId{};
    bool hasActivitySessionBinding{};
    bool activitySessionFromJoin{};
};

} // namespace dawn::server::bap::encrypted::transactions
