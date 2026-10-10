#pragma once
#include <algorithm>
#include "../../../core/settings/settings.h"
#include "../../../core/settings/address_text.h"

namespace dawn::client::network::native_routes {

// local startup contract; the live test must also verify the remote SignOn/descriptor bytes
[[nodiscard]] inline bool valid_contract(const core::settings::Settings& settings) noexcept {
    const auto& external = settings.client.externalServer;
    const auto& server = settings.server;
    std::array<unsigned char, 4> parsed{};
    const auto end = std::find(external.host.begin(), external.host.end(), '\0');
    if (!external.enabled || end == external.host.end()
        || external.portRouteCount > external.portRoutes.size()
        || !core::settings::address::parse_ipv4(
            {external.host.data(), static_cast<std::size_t>(end - external.host.begin())}, parsed)
        || parsed != external.address || parsed[0] == 0 || parsed[0] >= 224
        || !server.hasBapAdvertisedAddress || server.bapPort == 0
        || server.bapAdvertisedAddress[0] == 0 || server.bapAdvertisedAddress[0] >= 224)
        return false;
    const auto& gameplay = server.gameplay;
    const bool hasGameplay = gameplay.topology != core::settings::server::gameplay::Topology::disabled;
    if (hasGameplay && (gameplay.advertisedAddress != gameplay.transportAddress
        || gameplay.port == 0 || gameplay.advertisedAddress[0] == 0
        || gameplay.advertisedAddress[0] >= 224)) return false;
    const auto bapPort = server.bapAdvertisedPort != 0 ? server.bapAdvertisedPort : server.bapPort;
    for (std::size_t index = 0; index < external.portRouteCount; ++index) {
        const auto& route = external.portRoutes[index];
        if ((route.sourcePort == 3074 || route.sourcePort == 3075)
            && (route.address != external.address || route.targetPort != route.sourcePort))
            return false;
        if (route.sourcePort == server.bapPort
            && (route.address != server.bapAdvertisedAddress || route.targetPort != bapPort))
            return false;
        if (hasGameplay && route.sourcePort == gameplay.port
            && (route.address != gameplay.advertisedAddress || route.targetPort != gameplay.port))
            return false;
    }
    return true;
}
} // namespace dawn::client::network::native_routes
