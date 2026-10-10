#pragma once

#include <WinSock2.h>
#include <array>
#include <cstdio>

#include "server/bap/runtime.h"
#include "server/transport/bap_listener.h"
#include "server/transport/internal.h"

namespace restoration::tests {

inline bool dedicated_bap_capacity_self_test() noexcept {
    using namespace dawn;
    constexpr std::size_t expectedCapacity = 32;
    std::array<server::bap::ConnectionStatus, expectedCapacity + 1> statuses{};
    if (server::transport::g_listener.active || server::bap::snapshot_connections(statuses) != 0) {
        std::puts("FAIL: capacity fixture requires no existing listener or BAP sessions");
        return false;
    }
    if (!server::transport::initialize_on_port(0)) return false;
    struct Cleanup {
        std::array<SOCKET, expectedCapacity + 1> clients{};
        Cleanup() { clients.fill(INVALID_SOCKET); }
        ~Cleanup() {
            for (const SOCKET socketValue : clients) {
                if (socketValue != INVALID_SOCKET) closesocket(socketValue);
            }
            server::transport::shutdown();
        }
    } cleanup;
    sockaddr_in address{};
    int length = sizeof address;
    if (getsockname(server::transport::g_listener.acceptor,
                    reinterpret_cast<sockaddr*>(&address), &length) != 0
        || address.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || address.sin_port == 0) return false;
    const auto connect_client = [&](std::size_t index) noexcept {
        cleanup.clients[index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        return cleanup.clients[index] != INVALID_SOCKET
               && connect(cleanup.clients[index], reinterpret_cast<const sockaddr*>(&address),
                          sizeof address) == 0;
    };
    const auto wait_count = [&](std::size_t expected) noexcept {
        const auto deadline = GetTickCount64() + 500;
        do {
            server::transport::service(GetTickCount64());
            if (server::bap::snapshot_connections(statuses) == expected) return true;
            Sleep(1);
        } while (GetTickCount64() < deadline);
        return false;
    };
    for (std::size_t index = 0; index < expectedCapacity; ++index) {
        if (!connect_client(index) || !wait_count(index + 1)) {
            std::printf("FAIL: production listener accepted %zu/%zu connections; blocked slot=%zu\n",
                        server::bap::snapshot_connections(statuses), expectedCapacity, index + 1);
            return false;
        }
        if (statuses[index].connectionId != index + 1 || statuses[index].authenticated) return false;
    }
    if (!connect_client(expectedCapacity) || !wait_count(expectedCapacity)) return false;
    client::network::BapResponse response{};
    if (server::bap::consume({client::network::BapEvent::open, 0, {}, {}}, response)
        || server::bap::consume({client::network::BapEvent::open, expectedCapacity + 1, {}, {}}, response)) {
        std::puts("FAIL: invalid BAP connection id accepted");
        return false;
    }
    constexpr std::size_t releasedSlot = 16;
    closesocket(cleanup.clients[releasedSlot]);
    cleanup.clients[releasedSlot] = INVALID_SOCKET;
    if (!wait_count(expectedCapacity - 1) || !wait_count(expectedCapacity)) {
        std::puts("FAIL: pending connection did not reuse released listener/session slot");
        return false;
    }
    for (std::size_t index = 0; index < expectedCapacity; ++index) {
        if (statuses[index].connectionId != index + 1 || statuses[index].authenticated) return false;
    }
    server::transport::shutdown();
    if (server::bap::snapshot_connections(statuses) != 0) return false;
    std::printf("PASS: production BAP listener accepts %zu connections, bounds overflow and reuses freed slots\n",
                expectedCapacity);
    return true;
}

} // namespace restoration::tests
