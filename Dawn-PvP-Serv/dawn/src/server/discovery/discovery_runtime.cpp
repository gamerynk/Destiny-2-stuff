#include "../public_bind.h"
#include "discovery_runtime.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../core/logging/log.h"

namespace dawn::server::discovery {
namespace {

constexpr std::uint16_t kFirstPort = 3074;
constexpr std::uint16_t kSecondPort = 3075;
constexpr std::array<std::uint16_t, 2> kPorts{kFirstPort, kSecondPort};
constexpr unsigned kReceiveBudget = 32;
constexpr std::size_t kNatProbeRequestSize = 4;
constexpr std::size_t kNatProbeReplySize = 16;
constexpr std::size_t kIpDiscoveryRequestSize = 3;
constexpr std::size_t kIpDiscoveryReplySize = 9;
constexpr std::uint16_t kNatProbeMagic = 1;
constexpr std::uint16_t kFirstNatProbeIndex = 1;
constexpr std::uint16_t kSecondNatProbeIndex = 2;
constexpr unsigned kFirstIpDiscoveryType = 30;
constexpr unsigned kLastIpDiscoveryType = 39;
constexpr std::uint16_t kIpDiscoveryReplyVersion = 2;
constexpr std::uint32_t kNatProbeAddressMask = 0x76C3F6BC;
constexpr std::uint16_t kNatProbePortMask = 0xF6BC;
constexpr unsigned kReportBudget = 16;

enum class RequestKind {
    none,
    natProbe,
    ipDiscovery,
};

struct Listener {
    SOCKET socket{INVALID_SOCKET};
    std::uint16_t port{};
};

struct Runtime {
    std::array<Listener, kPorts.size()> listeners{};
    bool winsockOwned{};
    bool ready{};
    unsigned reports{};
};

Runtime g_runtime;

[[nodiscard]] std::uint16_t read_big_u16(std::span<const std::byte> input,
                                         std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(input[offset]) << 8U)
           | static_cast<std::uint16_t>(std::to_integer<unsigned>(input[offset + 1]));
}

void write_big_u16(std::span<std::byte> output, std::size_t offset, std::uint16_t value) noexcept {
    output[offset] = static_cast<std::byte>(value >> 8U);
    output[offset + 1] = static_cast<std::byte>(value);
}

void write_big_u32(std::span<std::byte> output, std::size_t offset, std::uint32_t value) noexcept {
    output[offset] = static_cast<std::byte>(value >> 24U);
    output[offset + 1] = static_cast<std::byte>(value >> 16U);
    output[offset + 2] = static_cast<std::byte>(value >> 8U);
    output[offset + 3] = static_cast<std::byte>(value);
}

void write_little_u16(std::span<std::byte> output,
                      std::size_t offset,
                      std::uint16_t value) noexcept {
    output[offset] = static_cast<std::byte>(value);
    output[offset + 1] = static_cast<std::byte>(value >> 8U);
}

[[nodiscard]] RequestKind classify(std::span<const std::byte> payload) noexcept {
    if (payload.size() == kNatProbeRequestSize && read_big_u16(payload, 0) == kNatProbeMagic) {
        const std::uint16_t index = read_big_u16(payload, 2);
        if (index == kFirstNatProbeIndex || index == kSecondNatProbeIndex) {
            return RequestKind::natProbe;
        }
    }
    if (payload.size() == kIpDiscoveryRequestSize) {
        const unsigned type = std::to_integer<unsigned>(payload.front());
        if (type >= kFirstIpDiscoveryType && type <= kLastIpDiscoveryType) {
            return RequestKind::ipDiscovery;
        }
    }
    return RequestKind::none;
}

[[nodiscard]] std::size_t build_reply(RequestKind kind,
                                      std::span<const std::byte> request,
                                      const sockaddr_in& source,
                                      std::span<std::byte> reply) noexcept {
    const std::uint32_t sourceIp = ntohl(source.sin_addr.s_addr);
    const std::uint16_t sourcePort = ntohs(source.sin_port);
    if (kind == RequestKind::natProbe) {
        reply[0] = request[0];
        reply[1] = request[1];
        reply[2] = request[2];
        reply[3] = request[3];
        write_big_u32(reply, 4, sourceIp ^ kNatProbeAddressMask);
        write_big_u16(reply, 8, sourcePort ^ kNatProbePortMask);
        return kNatProbeReplySize;
    }
    reply[0] = request[0];
    write_little_u16(reply, 1, kIpDiscoveryReplyVersion);
    write_big_u32(reply, 3, sourceIp);
    write_little_u16(reply, 7, sourcePort);
    return kIpDiscoveryReplySize;
}

[[nodiscard]] bool make_nonblocking(SOCKET socket) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
}

void close_all() noexcept {
    for (Listener& listener : g_runtime.listeners) {
        if (listener.socket != INVALID_SOCKET) {
            closesocket(listener.socket);
            listener.socket = INVALID_SOCKET;
        }
        listener.port = 0;
    }
    if (g_runtime.winsockOwned) {
        WSACleanup();
        g_runtime.winsockOwned = false;
    }
    g_runtime.ready = false;
    g_runtime.reports = 0;
}

[[nodiscard]] bool bind_listener(Listener& listener, std::uint16_t port) noexcept {
    listener.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listener.socket == INVALID_SOCKET) {
        return false;
    }
    const BOOL exclusive = TRUE;
    if (setsockopt(listener.socket,
                   SOL_SOCKET,
                   SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive),
                   sizeof exclusive)
            == SOCKET_ERROR
        || !make_nonblocking(listener.socket)) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(server::public_bind_host_order());
    if (bind(listener.socket, reinterpret_cast<const sockaddr*>(&address), sizeof address)
        == SOCKET_ERROR) {
        return false;
    }
    listener.port = port;
    return true;
}

void service_listener(Listener& listener) noexcept {
    for (unsigned drained = 0; drained < kReceiveBudget; ++drained) {
        std::array<std::byte, kNatProbeReplySize> request{};
        sockaddr_in source{};
        int sourceSize = sizeof source;
        const int received = recvfrom(listener.socket,
                                      reinterpret_cast<char*>(request.data()),
                                      static_cast<int>(request.size()),
                                      0,
                                      reinterpret_cast<sockaddr*>(&source),
                                      &sourceSize);
        if (received == SOCKET_ERROR) {
            break;
        }
        if (received <= 0 || sourceSize != sizeof source || source.sin_family != AF_INET) {
            continue;
        }
        const auto payload = std::span<const std::byte>(
            request.data(), static_cast<std::size_t>(received));
        const RequestKind kind = classify(payload);
        if (kind == RequestKind::none) {
            continue;
        }
        std::array<std::byte, kNatProbeReplySize> reply{};
        const std::size_t replySize = build_reply(kind, payload, source, reply);
        const int sent = sendto(listener.socket,
                                reinterpret_cast<const char*>(reply.data()),
                                static_cast<int>(replySize),
                                0,
                                reinterpret_cast<const sockaddr*>(&source),
                                sizeof source);
        const bool succeeded = sent == static_cast<int>(replySize);
        if (g_runtime.reports < kReportBudget) {
            ++g_runtime.reports;
            core::log::write(core::log::Channel::server,
                             succeeded ? core::log::Level::info : core::log::Level::warn,
                             kind == RequestKind::natProbe
                                 ? (succeeded
                                        ? "ev=discovery stage=reply result=ok kind=nat_probe"
                                        : "ev=discovery stage=reply result=fail kind=nat_probe")
                                 : (succeeded ? "ev=discovery stage=reply result=ok kind=ip"
                                              : "ev=discovery stage=reply result=fail kind=ip"));
        }
    }
}

} // namespace

bool initialize() noexcept {
    if (g_runtime.ready) {
        return true;
    }
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=discovery stage=listen result=fail reason=winsock");
        return false;
    }
    g_runtime.winsockOwned = true;
    for (std::size_t index = 0; index < g_runtime.listeners.size(); ++index) {
        if (!bind_listener(g_runtime.listeners[index], kPorts[index])) {
            close_all();
            core::log::write(core::log::Channel::server,
                             core::log::Level::error,
                             index == 0
                                 ? "ev=discovery stage=listen result=fail reason=bind port=3074"
                                 : "ev=discovery stage=listen result=fail reason=bind port=3075");
            return false;
        }
    }
    g_runtime.ready = true;
    core::log::write(core::log::Channel::server,
                     core::log::Level::info,
                     "ev=discovery stage=listen result=ok ports=3074,3075");
    return true;
}

void service() noexcept {
    if (!g_runtime.ready) {
        return;
    }
    for (Listener& listener : g_runtime.listeners) {
        service_listener(listener);
    }
}

void shutdown() noexcept {
    close_all();
}

bool ready() noexcept {
    return g_runtime.ready;
}

} // namespace dawn::server::discovery
