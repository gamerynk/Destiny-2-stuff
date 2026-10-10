#pragma once

#include "../../../middleware/gameplay/group/parameter_messages.h"
#include "../../../state/session/native_bap_endpoint.h"

namespace dawn::server::gameplay::group {

inline void publish_activity_host_endpoint(
    middleware::gameplay::group::ActivityHostParameter& body,
    const core::settings::Settings& settings) noexcept {
    state::SignOnState endpoint{};
    state::session::publish_native_bap_endpoint(endpoint, settings);
    // group codec 1768E90 reads a numeric32 field but svc17 copies four octets; keep the host-order
    // contract (127.0.0.1 == 0x7f000001) and resolve the destination via the same producer
    const auto raw = endpoint.relayAddress;
    body.address = ((raw & 0x000000FFU) << 24U) | ((raw & 0x0000FF00U) << 8U)
                 | ((raw & 0x00FF0000U) >> 8U) | ((raw & 0xFF000000U) >> 24U);
    body.port = endpoint.relayPort;
}

} // namespace dawn::server::gameplay::group
