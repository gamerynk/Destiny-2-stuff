#include "gameplay_endpoint.h"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <array>
#include <atomic>

#include "../../../core/settings/settings.h"
#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/gameplay/nat/introduction.h"
#include "../association/association_host.h"
#include "../dtls/dtls_host.h"
#include "../gameplay_log.h"

namespace dawn::server::gameplay::endpoint {

namespace {

namespace settings = core::settings::server::gameplay;

constexpr unsigned kReceiveBudget = 32;
constexpr std::size_t kDatagramCapacity = 1500;
constexpr unsigned kMaxReceiveReports = 64;
constexpr unsigned kMaxTraversalReports = 8;

std::atomic<unsigned> g_reported{0};
std::atomic<unsigned> g_traversalReported{0};

struct Endpoint {
    SOCKET socket{INVALID_SOCKET};
    bool winsockOwned{};
    bool ready{};
    state::gameplay::Endpoint advertised{};
    Identity identity{};
};

[[nodiscard]] bool generate_identity(std::uint64_t& output) noexcept {
    constexpr unsigned kByteBits = 8;
    std::array<std::byte, sizeof(std::uint64_t)> bytes{};
    if (!middleware::crypto::random::fill(bytes)) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[index]) << (index * kByteBits);
    }
    output = value == 0 ? 1 : value;
    return true;
}

SRWLOCK g_lock{SRWLOCK_INIT};
Endpoint g_endpoint;

[[nodiscard]] std::uint32_t
host_address(const std::array<unsigned char, settings::kAddressOctets>& octets) noexcept {
    constexpr unsigned kOctetBits = 8;
    std::uint32_t value = 0;
    for (const unsigned char octet : octets) {
        value = (value << kOctetBits) | octet;
    }
    return value;
}

[[nodiscard]] bool make_nonblocking(SOCKET socket) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
}

void close_locked() noexcept {
    if (g_endpoint.socket != INVALID_SOCKET) {
        closesocket(g_endpoint.socket);
        g_endpoint.socket = INVALID_SOCKET;
    }
    if (g_endpoint.winsockOwned) {
        WSACleanup();
        g_endpoint.winsockOwned = false;
    }
}

[[nodiscard]] bool bind_embedded(const settings::Settings& configured) noexcept {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return false;
    }
    g_endpoint.winsockOwned = true;
    g_endpoint.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_endpoint.socket == INVALID_SOCKET) {
        close_locked();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(configured.port);
    address.sin_addr.s_addr = htonl(host_address(configured.bindAddress));
    if (!make_nonblocking(g_endpoint.socket)
        || bind(g_endpoint.socket, reinterpret_cast<const sockaddr*>(&address), sizeof address)
               == SOCKET_ERROR) {
        close_locked();
        return false;
    }
    return true;
}

// release the lock before routing: routing sends replies through this endpoint and the lock isn't recursive
[[nodiscard]] bool receive_once(std::span<std::byte> buffer,
                                state::gameplay::Endpoint& from,
                                std::size_t& size) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_endpoint.socket == INVALID_SOCKET) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    sockaddr_in source{};
    int sourceSize = sizeof source;
    const int received = recvfrom(g_endpoint.socket,
                                  reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()),
                                  0,
                                  reinterpret_cast<sockaddr*>(&source),
                                  &sourceSize);
    ReleaseSRWLockExclusive(&g_lock);
    if (received <= 0) {
        return false;
    }
    from.address = ntohl(source.sin_addr.s_addr);
    from.port = ntohs(source.sin_port);
    size = static_cast<std::size_t>(received);
    return true;
}

} // namespace

bool initialize() noexcept {
    const settings::Settings& configured = core::settings::get().server.gameplay;
    AcquireSRWLockExclusive(&g_lock);
    if (g_endpoint.ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (!settings::valid(configured)) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=settings");
        return false;
    }
    if (configured.topology == settings::Topology::disabled) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::info, "ev=gameplay stage=endpoint result=skip reason=disabled");
        return true;
    }
    // the descriptor advertises one endpoint and the egress rewrite keeps its port, so the transport
    // endpoint must land on the port the socket binds
    if (configured.topology == settings::Topology::embedded && !bind_embedded(configured)) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=bind");
        return false;
    }
    if (!generate_identity(g_endpoint.identity.machineId)
        || !generate_identity(g_endpoint.identity.onlineSessionId)) {
        close_locked();
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=identity");
        return false;
    }
    g_endpoint.advertised.address = host_address(configured.advertisedAddress);
    g_endpoint.advertised.port = configured.port;
    g_endpoint.ready = true;
    const bool embedded = configured.topology == settings::Topology::embedded;
    ReleaseSRWLockExclusive(&g_lock);
    report(core::log::Level::info,
           "ev=gameplay stage=endpoint result=ok mode=%s port=%u",
           embedded ? "embedded" : "external",
           static_cast<unsigned>(configured.port));
    return true;
}

void service(std::uint64_t now) noexcept {
    for (unsigned drained = 0; drained < kReceiveBudget; ++drained) {
        std::array<std::byte, kDatagramCapacity> buffer{};
        state::gameplay::Endpoint from{};
        std::size_t size = 0;
        if (!receive_once(buffer, from, size)) {
            break;
        }
        // routing only reports what it recognises, so an arrival and a silent drop would read the same
        if (g_reported.fetch_add(1, std::memory_order_relaxed) < kMaxReceiveReports) {
            report(core::log::Level::info,
                   "ev=gameplay stage=receive result=ok from=0x%08X:%u bytes=%zu b0=0x%02X",
                   from.address,
                   static_cast<unsigned>(from.port),
                   size,
                   size != 0 ? static_cast<unsigned>(buffer[0]) : 0U);
        }
        // answer traversal before routing: the association layer has no reader for it and the client
        // never resolves this address until it's answered
        if (middleware::gameplay::nat::make_introduction_reply({buffer.data(), size})) {
            const bool sent = send_to(from, {buffer.data(), size});
            if (g_traversalReported.fetch_add(1, std::memory_order_relaxed)
                < kMaxTraversalReports) {
                report(core::log::Level::info,
                       "ev=gameplay stage=traversal result=%s from=0x%08X:%u",
                       sent ? "reply" : "send_failed",
                       from.address,
                       static_cast<unsigned>(from.port));
            }
            continue;
        }
        // the peer opens every connection with the Demonware association handshake, so this runs ahead
        // of the engine association on the same port
        if (dtls::route(from, {buffer.data(), size}, now)) {
            continue;
        }
        association::route(from, {buffer.data(), size}, now);
    }
    association::expire(now);
}

bool send_to(const state::gameplay::Endpoint& destination,
             std::span<const std::byte> datagram) noexcept {
    if (datagram.empty() || destination.port == 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(destination.port);
    address.sin_addr.s_addr = htonl(destination.address);
    AcquireSRWLockShared(&g_lock);
    const SOCKET socket = g_endpoint.socket;
    const int sent = socket == INVALID_SOCKET
                         ? SOCKET_ERROR
                         : sendto(socket,
                                  reinterpret_cast<const char*>(datagram.data()),
                                  static_cast<int>(datagram.size()),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&address),
                                  sizeof address);
    ReleaseSRWLockShared(&g_lock);
    return sent == static_cast<int>(datagram.size());
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    close_locked();
    g_endpoint.ready = false;
    g_endpoint.advertised = {};
    g_endpoint.identity = {};
    ReleaseSRWLockExclusive(&g_lock);
}

bool ready() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool value = g_endpoint.ready;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

state::gameplay::Endpoint advertised() noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::Endpoint value = g_endpoint.advertised;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

Identity identity() noexcept {
    AcquireSRWLockShared(&g_lock);
    const Identity value = g_endpoint.identity;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

std::uint64_t lobby_machine_id(std::uint64_t lobbyId) noexcept {
    const Identity current = identity();
    return derive_lobby_machine_id(current.machineId, lobbyId);
}

bool build_lobby_descriptor(
    std::uint64_t lobbyId,
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize>& output) noexcept {
    output = {};
    if (lobbyId == 0) {
        return false;
    }

    state::gameplay::Endpoint advertisedEndpoint{};
    Identity endpointIdentity{};
    bool endpointReady = false;
    AcquireSRWLockShared(&g_lock);
    endpointReady = g_endpoint.ready;
    advertisedEndpoint = g_endpoint.advertised;
    endpointIdentity = g_endpoint.identity;
    ReleaseSRWLockShared(&g_lock);
    if (!endpointReady) {
        return false;
    }

    middleware::gameplay::descriptor::JoinEndpoint join{};
    join.machineId = derive_lobby_machine_id(endpointIdentity.machineId, lobbyId);
    join.address = advertisedEndpoint.address;
    join.port = advertisedEndpoint.port;
    join.onlineSessionId = lobbyId;
    return middleware::gameplay::descriptor::build(join, output);
}

} // namespace dawn::server::gameplay::endpoint
