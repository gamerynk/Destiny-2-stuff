#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::bap::activity_host_manager {

struct Request final {
    std::span<const std::byte> protobuf{};
};

} // namespace dawn::middleware::bap::activity_host_manager
