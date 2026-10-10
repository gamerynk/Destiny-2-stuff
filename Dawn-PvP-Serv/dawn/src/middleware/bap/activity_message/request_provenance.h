#pragma once
#include <cstdint>
namespace dawn::middleware::bap::activity_message {
// supplied by the authenticated server transport, never parsed from svc8 data; taskId is an
// opaque correlation id, not a proven native sequence
struct RequestProvenance final {
    std::uint64_t connectionIncarnation{},authenticatedOrdinal{};
    std::uint32_t taskId{};
    bool valid() const noexcept {return connectionIncarnation&&authenticatedOrdinal;}
};
}
