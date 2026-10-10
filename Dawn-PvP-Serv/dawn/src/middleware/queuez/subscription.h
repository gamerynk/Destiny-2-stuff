#pragma once

#include <cstdint>

namespace dawn::middleware::queuez {

struct Subscription {
    std::uint32_t familyType{};
    std::uint64_t familyRootSoid{};
};

} // namespace dawn::middleware::queuez
