#include <limits>
#include <intrin.h>

#include "../../../../../core/logging/log.h"
#include "../../internal.h"
#include "../../policy/policy.h"
#include "../discovery/egress_discovery_responder.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::winsock::transmission {
namespace {

// a null destination means the connected peer only when the address length is 0
constexpr int kConnectedDestinationLength = 0;

void clear_bytes(LPDWORD bytes) noexcept {
    if (bytes != nullptr) {
        *bytes = 0;
    }
}

[[nodiscard]] std::size_t buffer_bytes(LPWSABUF buffers, DWORD count) noexcept {
    std::size_t total = 0;
    if (buffers != nullptr && count <= 64U) {
        for (DWORD index = 0; index < count; ++index) {
            total += buffers[index].len;
        }
    }
    return total;
}

[[nodiscard]] bool prepare_destination(SOCKET socket,
                                       const sockaddr* destination,
                                       int destinationLength,
                                       sockaddr_in& redirected,
                                       sockaddr*& forwardedDestination,
                                       int& forwardedLength) noexcept {
    if (destination != nullptr) {
        if (!policy::redirect_ipv4(destination, destinationLength, redirected)) {
            return false;
        }
        forwardedDestination = reinterpret_cast<sockaddr*>(&redirected);
        forwardedLength = static_cast<int>(sizeof(redirected));
        return true;
    }
    forwardedDestination = nullptr;
    forwardedLength = kConnectedDestinationLength;
    return destinationLength == kConnectedDestinationLength
           && policy::has_redirect_target_peer(socket);
}

void log_discovery(bool succeeded) noexcept {
    if (succeeded) {
        return;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::warn,
                     "ev=egress stage=discovery target=redirect action=respond result=fail");
}

[[nodiscard]] discovery::Result
handle_buffer_discovery(SOCKET socket,
                        LPWSABUF buffers,
                        DWORD bufferCount,
                        DWORD flags,
                        const sockaddr* destination,
                        int destinationLength,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion,
                        discovery::SendTo sendTo) noexcept {
    if (buffers == nullptr || bufferCount != 1 || overlapped != nullptr || completion != nullptr
        || buffers[0].len > static_cast<ULONG>((std::numeric_limits<int>::max)())) {
        return {};
    }
    return discovery::handle(socket,
                             std::as_bytes(std::span(buffers[0].buf, buffers[0].len)),
                             static_cast<int>(flags),
                             destination,
                             destinationLength,
                             sendTo);
}

} // namespace

int WSAAPI send_bytes_to(SOCKET socket,
                         const char* buffer,
                         int length,
                         int flags,
                         const sockaddr* destination,
                         int destinationLength) noexcept {
    const auto call = original<decltype(&::sendto)>(HookSlot::sendTo);
    const void* const caller = _ReturnAddress();
    const bool connectedRoute = destination == nullptr && destinationLength == 0;
    if (call != nullptr && policy::is_native_sdr_udp_socket(socket)
        && (policy::is_native_sdr_ipv4_target(destination, destinationLength)
            || (connectedRoute && policy::has_native_sdr_ipv4_peer(socket)))
        && policy::allow_native_sdr_passthrough(caller)) {
        const int result = call(socket, buffer, length, flags, destination, destinationLength);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::sendTo, connectedRoute,
                                     length > 0 ? static_cast<std::size_t>(length) : 0U,
                                     result, error);
        WSASetLastError(error);
        return result;
    }
    if (buffer != nullptr && length >= 0) {
        const discovery::Result discoveryResult =
            discovery::handle(socket,
                              std::as_bytes(std::span(buffer, static_cast<std::size_t>(length))),
                              flags,
                              destination,
                              destinationLength,
                              call);
        if (discoveryResult.handled) {
            log_discovery(discoveryResult.result != SOCKET_ERROR);
            return discoveryResult.result;
        }
    }

    sockaddr_in redirected{};
    sockaddr* forwardedDestination = nullptr;
    int forwardedLength = 0;
    const bool targetsRedirect = prepare_destination(
        socket, destination, destinationLength, redirected, forwardedDestination, forwardedLength);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    return call(socket, buffer, length, flags, forwardedDestination, forwardedLength);
}

int WSAAPI send_buffers_to(SOCKET socket,
                           LPWSABUF buffers,
                           DWORD bufferCount,
                           LPDWORD bytesSent,
                           DWORD flags,
                           const sockaddr* destination,
                           int destinationLength,
                           LPWSAOVERLAPPED overlapped,
                           LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSASendTo)>(HookSlot::wsaSendTo);
    const void* const caller = _ReturnAddress();
    const bool connectedRoute = destination == nullptr && destinationLength == 0;
    if (call != nullptr && policy::is_native_sdr_udp_socket(socket)
        && (policy::is_native_sdr_ipv4_target(destination, destinationLength)
            || (connectedRoute && policy::has_native_sdr_ipv4_peer(socket)))
        && policy::allow_native_sdr_passthrough(caller)) {
        const std::size_t submitted = buffer_bytes(buffers, bufferCount);
        const int result = call(socket, buffers, bufferCount, bytesSent, flags,
                                destination, destinationLength, overlapped, completion);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::wsaSendTo, connectedRoute,
                                     submitted, result, error);
        WSASetLastError(error);
        return result;
    }
    const auto sendTo = original<decltype(&::sendto)>(HookSlot::sendTo);
    const discovery::Result discoveryResult = handle_buffer_discovery(socket,
                                                                      buffers,
                                                                      bufferCount,
                                                                      flags,
                                                                      destination,
                                                                      destinationLength,
                                                                      overlapped,
                                                                      completion,
                                                                      sendTo);
    if (discoveryResult.handled) {
        const bool succeeded = discoveryResult.result != SOCKET_ERROR;
        if (bytesSent != nullptr) {
            *bytesSent = succeeded ? buffers[0].len : 0;
        }
        log_discovery(succeeded);
        return succeeded ? 0 : SOCKET_ERROR;
    }

    sockaddr_in redirected{};
    sockaddr* forwardedDestination = nullptr;
    int forwardedLength = 0;
    const bool targetsRedirect = prepare_destination(
        socket, destination, destinationLength, redirected, forwardedDestination, forwardedLength);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        clear_bytes(bytesSent);
        return policy::deny_socket_call();
    }
    return call(socket,
                buffers,
                bufferCount,
                bytesSent,
                flags,
                forwardedDestination,
                forwardedLength,
                overlapped,
                completion);
}

} // namespace dawn::client::hooks::egress::winsock::transmission
