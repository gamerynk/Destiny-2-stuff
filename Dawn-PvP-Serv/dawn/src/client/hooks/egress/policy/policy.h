#pragma once

#include <array>
#include <string_view>

#include "../platform/sdk.h"

namespace dawn::client::hooks::egress::policy {

enum class SocketOperation : unsigned char {
    connect,
    send,
};

enum class NativeSdrPath : unsigned char {
    connect,
    send,
    wsaSend,
    sendTo,
    wsaSendTo,
    wsaSendMsg,
    recvFrom,
    wsaRecvFrom,
    count,
};

enum class NameOperation : unsigned char {
    resolve,
    reverse,
    dns,
    control,
};

void log_name_decision(NameOperation operation,
                       bool allowed,
                       std::string_view name = {},
                       unsigned selector = 0,
                       unsigned status = 0,
                       const void* caller = nullptr) noexcept;

void log_name_decision(NameOperation operation,
                       bool allowed,
                       std::wstring_view name,
                       unsigned selector = 0,
                       unsigned status = 0,
                       const void* caller = nullptr) noexcept;

[[nodiscard]] std::array<unsigned char, 4> redirect_octets() noexcept;

[[nodiscard]] bool is_redirect_target(const sockaddr* address, int addressLength) noexcept;

[[nodiscard]] bool is_native_sdr_ipv4_target(const sockaddr* address,
                                              int addressLength) noexcept;

[[nodiscard]] bool has_native_sdr_ipv4_peer(SOCKET socket) noexcept;

[[nodiscard]] bool is_native_sdr_udp_socket(SOCKET socket) noexcept;

[[nodiscard]] bool
redirect_ipv4(const sockaddr* address, int addressLength, sockaddr_in& redirected) noexcept;

[[nodiscard]] bool has_redirect_target_peer(SOCKET socket) noexcept;

[[nodiscard]] bool allow_native_sdr_passthrough(const void* caller) noexcept;

void report_native_sdr_io(NativeSdrPath path,
                          bool connectedRoute,
                          std::size_t bytes,
                          int apiReturn,
                          int socketError) noexcept;

[[nodiscard]] bool
allow_socket_call(SocketOperation operation, bool targetsRedirect, bool originalAvailable) noexcept;

[[nodiscard]] int deny_socket_call() noexcept;

} // namespace dawn::client::hooks::egress::policy
