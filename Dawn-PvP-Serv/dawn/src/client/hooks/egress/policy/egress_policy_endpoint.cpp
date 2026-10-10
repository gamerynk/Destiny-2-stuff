#include <array>
#include <cstring>

#include "../../../../core/settings/settings.h"
#include "egress_policy_logging.h"

namespace dawn::client::hooks::egress::policy {
namespace {

constexpr int kBlockedSocketError = WSAEACCES;
constexpr std::array<unsigned char, 4> kLoopbackOctets{127, 0, 0, 1};

[[nodiscard]] const core::settings::client::external::Settings*
external_settings() noexcept {
    const auto& external = core::settings::get().client.externalServer;
    return external.enabled ? &external : nullptr;
}

[[nodiscard]] const core::settings::client::external::PortRoute*
find_port_route(std::uint16_t sourcePort) noexcept {
    const auto* const external = external_settings();
    if (external == nullptr) {
        return nullptr;
    }
    for (std::size_t index = 0; index < external->portRouteCount; ++index) {
        if (external->portRoutes[index].sourcePort == sourcePort) {
            return &external->portRoutes[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool is_port_route_target(const sockaddr_in& endpoint) noexcept {
    const auto* const external = external_settings();
    if (external == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < external->portRouteCount; ++index) {
        const auto& route = external->portRoutes[index];
        if (endpoint.sin_port == htons(route.targetPort)
            && std::memcmp(&endpoint.sin_addr, route.address.data(), route.address.size()) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::array<unsigned char, 4> redirect_octets() noexcept {
    const core::settings::client::external::Settings& external =
        core::settings::get().client.externalServer;
    if (!external.enabled) {
        return kLoopbackOctets;
    }
    return {external.address[0], external.address[1], external.address[2], external.address[3]};
}

bool is_redirect_target(const sockaddr* address, int addressLength) noexcept {
    if (address == nullptr || addressLength < static_cast<int>(sizeof(sockaddr_in))) {
        return false;
    }

    sockaddr_in endpoint{};
    std::memcpy(&endpoint, address, sizeof(endpoint));
    const std::array<unsigned char, 4> octets = redirect_octets();
    if (endpoint.sin_family != AF_INET) {
        return false;
    }
    if (std::memcmp(&endpoint.sin_addr, octets.data(), octets.size()) == 0) {
        return true;
    }

    return is_port_route_target(endpoint);
}

bool is_native_sdr_ipv4_target(const sockaddr* address, int addressLength) noexcept {
    if (address == nullptr || addressLength < static_cast<int>(sizeof(sockaddr_in))) {
        return false;
    }
    sockaddr_in endpoint{};
    std::memcpy(&endpoint, address, sizeof(endpoint));
    return endpoint.sin_family == AF_INET;
}

bool has_native_sdr_ipv4_peer(SOCKET socket) noexcept {
    sockaddr_storage peer{};
    int peerLength = static_cast<int>(sizeof(peer));
    return ::getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peerLength) != SOCKET_ERROR
           && is_native_sdr_ipv4_target(reinterpret_cast<const sockaddr*>(&peer), peerLength);
}

bool is_native_sdr_udp_socket(SOCKET socket) noexcept {
    int type = 0;
    int length = static_cast<int>(sizeof(type));
    return ::getsockopt(socket, SOL_SOCKET, SO_TYPE,
                        reinterpret_cast<char*>(&type), &length) == 0
           && length == static_cast<int>(sizeof(type)) && type == SOCK_DGRAM;
}

bool redirect_ipv4(const sockaddr* address, int addressLength, sockaddr_in& redirected) noexcept {
    if (address == nullptr || addressLength < static_cast<int>(sizeof(redirected))) {
        return false;
    }

    std::memcpy(&redirected, address, sizeof(redirected));
    if (redirected.sin_family != AF_INET) {
        return false;
    }
    // NAT traversal replies name the public endpoint; keep it on later DTLS sends instead of
    // applying the default host route twice
    if (is_port_route_target(redirected)) {
        return true;
    }
    std::array<unsigned char, 4> octets = redirect_octets();
    if (const auto* const route = find_port_route(ntohs(redirected.sin_port)); route != nullptr) {
        octets = route->address;
        redirected.sin_port = htons(route->targetPort);
    }
    std::memcpy(&redirected.sin_addr, octets.data(), octets.size());
    return true;
}

bool has_redirect_target_peer(SOCKET socket) noexcept {
    sockaddr_storage peer{};
    int peerLength = static_cast<int>(sizeof(peer));
    if (::getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peerLength) == SOCKET_ERROR) {
        return false;
    }
    return is_redirect_target(reinterpret_cast<const sockaddr*>(&peer), peerLength);
}

bool allow_socket_call(SocketOperation operation,
                       bool targetsRedirect,
                       bool originalAvailable) noexcept {
    const bool allowed = targetsRedirect && originalAvailable;
    log_decision(operation, targetsRedirect, allowed);
    return allowed;
}

int deny_socket_call() noexcept {
    WSASetLastError(kBlockedSocketError);
    return SOCKET_ERROR;
}

} // namespace dawn::client::hooks::egress::policy
