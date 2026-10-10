#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "client/hooks/egress/policy/policy.h"
#include "core/settings/settings.h"

namespace dawn::core::settings {

Settings g_endpointTestSettings{};

const Settings& get() noexcept {
    return g_endpointTestSettings;
}

} // namespace dawn::core::settings

namespace dawn::client::hooks::egress::policy {

void log_decision(SocketOperation, bool, bool) noexcept {}

} // namespace dawn::client::hooks::egress::policy

namespace {

using dawn::client::hooks::egress::policy::redirect_ipv4;
using dawn::client::hooks::egress::policy::is_native_sdr_ipv4_target;

[[nodiscard]] bool require(bool condition, const char* description) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

[[nodiscard]] sockaddr_in endpoint(std::array<unsigned char, 4> address,
                                   std::uint16_t port) noexcept {
    sockaddr_in value{};
    value.sin_family = AF_INET;
    value.sin_port = htons(port);
    std::memcpy(&value.sin_addr, address.data(), address.size());
    return value;
}

[[nodiscard]] bool equals(const sockaddr_in& value,
                          std::array<unsigned char, 4> address,
                          std::uint16_t port) noexcept {
    return value.sin_family == AF_INET && value.sin_port == htons(port)
           && std::memcmp(&value.sin_addr, address.data(), address.size()) == 0;
}

} // namespace

int main() {
    constexpr std::array<unsigned char, 4> kLoopback{127, 0, 0, 1};
    constexpr std::array<unsigned char, 4> kDefaultHost{147, 185, 221, 230};
    constexpr std::array<unsigned char, 4> kGameplayHost{69, 9, 180, 16};
    constexpr std::uint16_t kNativePort = 30976;
    constexpr std::uint16_t kPublicPort = 3925;

    auto& external = dawn::core::settings::g_endpointTestSettings.client.externalServer;
    external.enabled = true;
    external.address = kDefaultHost;
    external.portRouteCount = 1;
    external.portRoutes[0] = {kNativePort, kGameplayHost, kPublicPort};

    bool passed = true;
    const sockaddr_in native = endpoint(kLoopback, kNativePort);
    passed &= require(is_native_sdr_ipv4_target(reinterpret_cast<const sockaddr*>(&native),
                                                sizeof native),
                      "native SDR accepts complete IPv4 destination");
    passed &= require(!is_native_sdr_ipv4_target(reinterpret_cast<const sockaddr*>(&native),
                                                 sizeof native - 1),
                      "native SDR rejects truncated IPv4 destination");
    sockaddr_in ipv6Family = native;
    ipv6Family.sin_family = AF_INET6;
    passed &= require(!is_native_sdr_ipv4_target(reinterpret_cast<const sockaddr*>(&ipv6Family),
                                                 sizeof ipv6Family),
                      "native SDR rejects other address families");
    passed &= require(!is_native_sdr_ipv4_target(nullptr, sizeof native),
                      "native SDR rejects null destination");
    sockaddr_in routed{};
    passed &= require(redirect_ipv4(reinterpret_cast<const sockaddr*>(&native),
                                    sizeof native,
                                    routed),
                      "native gameplay endpoint routes");
    passed &= require(equals(routed, kGameplayHost, kPublicPort),
                      "native gameplay endpoint uses public route address and port");

    sockaddr_in routedAgain{};
    passed &= require(redirect_ipv4(reinterpret_cast<const sockaddr*>(&routed),
                                    sizeof routed,
                                    routedAgain),
                      "public route target is accepted");
    passed &= require(std::memcmp(&routedAgain, &routed, sizeof routed) == 0,
                      "public route target is idempotent for later secure sends");

    const sockaddr_in defaultHostNativePort = endpoint(kDefaultHost, kNativePort);
    sockaddr_in rerouted{};
    passed &= require(redirect_ipv4(reinterpret_cast<const sockaddr*>(&defaultHostNativePort),
                                    sizeof defaultHostNativePort,
                                    rerouted),
                      "default-host native endpoint routes");
    passed &= require(equals(rerouted, kGameplayHost, kPublicPort),
                      "default-host address does not suppress a native-port route");

    const sockaddr_in sameAddressWrongPort = endpoint(kGameplayHost, kPublicPort + 1U);
    sockaddr_in wrongPortResult{};
    passed &= require(redirect_ipv4(reinterpret_cast<const sockaddr*>(&sameAddressWrongPort),
                                    sizeof sameAddressWrongPort,
                                    wrongPortResult),
                      "same-address wrong-port endpoint is accepted");
    passed &= require(equals(wrongPortResult, kDefaultHost, kPublicPort + 1U),
                      "route-target idempotence requires both address and port");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: external endpoint routing is idempotent after NAT traversal");
    return 0;
}
