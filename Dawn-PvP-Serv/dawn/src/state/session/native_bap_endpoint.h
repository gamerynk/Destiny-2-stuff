#pragma once

#include "../../core/settings/settings.h"
#include "../runtime/state.h"

namespace dawn::state::session {

inline void publish_native_bap_endpoint(SignOnState& signOn,
                                        const core::settings::Settings& settings) noexcept {
    // retail stores the decoded integer straight into the IPv4 address, no htonl; on x64
    // little-endian the first octet is the low byte
    constexpr std::uint32_t kLoopbackAddress = 0x0100007FU;
    std::array<unsigned char, 4> address{127, 0, 0, 1};
    signOn.relayPort = settings.server.bapPort;
    const auto& external = settings.client.externalServer;
    if (settings.server.hasBapAdvertisedAddress && signOn.relayPort != 0) {
        address = settings.server.bapAdvertisedAddress;
        if (settings.server.bapAdvertisedPort != 0)
            signOn.relayPort = settings.server.bapAdvertisedPort;
    } else if (external.enabled && signOn.relayPort != 0) {
        address = external.address;
        for (std::size_t index = 0; index < external.portRouteCount; ++index) {
            const auto& route = external.portRoutes[index];
            if (route.sourcePort == signOn.relayPort) {
                address = route.address;
                signOn.relayPort = route.targetPort;
                break;
            }
        }
    }
    signOn.relayAddress = signOn.relayPort == 0 ? kLoopbackAddress
        : static_cast<std::uint32_t>(address[0])
          | (static_cast<std::uint32_t>(address[1]) << 8U)
          | (static_cast<std::uint32_t>(address[2]) << 16U)
          | (static_cast<std::uint32_t>(address[3]) << 24U);
}

} // namespace dawn::state::session
