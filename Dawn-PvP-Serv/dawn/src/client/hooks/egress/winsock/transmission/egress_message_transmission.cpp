#include <intrin.h>

#include "../../internal.h"
#include "../../policy/policy.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::winsock::transmission {
namespace {

void clear_bytes(LPDWORD bytes) noexcept {
    if (bytes != nullptr) {
        *bytes = 0;
    }
}

[[nodiscard]] std::size_t submitted_bytes(const WSAMSG& message) noexcept {
    std::size_t total = 0;
    if (message.lpBuffers != nullptr && message.dwBufferCount <= 64U) {
        for (DWORD index = 0; index < message.dwBufferCount; ++index) {
            total += message.lpBuffers[index].len;
        }
    }
    return total;
}

} // namespace

INT PASCAL send_message(SOCKET socket,
                        LPWSAMSG message,
                        DWORD flags,
                        LPDWORD bytesSent,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSASendMsg)>(HookSlot::wsaSendMsg);
    if (message == nullptr) {
        clear_bytes(bytesSent);
        (void)policy::allow_socket_call(policy::SocketOperation::send, false, call != nullptr);
        return policy::deny_socket_call();
    }
    const void* const caller = _ReturnAddress();
    const bool nativeTarget = message->name != nullptr
                                  ? policy::is_native_sdr_ipv4_target(message->name,
                                                                       message->namelen)
                                  : policy::has_native_sdr_ipv4_peer(socket);
    if (call != nullptr && nativeTarget && policy::is_native_sdr_udp_socket(socket)
        && policy::allow_native_sdr_passthrough(caller)) {
        const bool connectedRoute = message->name == nullptr;
        const std::size_t submitted = submitted_bytes(*message);
        const INT result = call(socket, message, flags, bytesSent, overlapped, completion);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::wsaSendMsg,
                                     connectedRoute, submitted, result, error);
        WSASetLastError(error);
        return result;
    }
    if (message->name == nullptr) {
        const bool targetsRedirect = policy::has_redirect_target_peer(socket);
        if (call == nullptr
            || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
            clear_bytes(bytesSent);
            return policy::deny_socket_call();
        }
        return call(socket, message, flags, bytesSent, overlapped, completion);
    }

    sockaddr_in redirectedAddress{};
    const bool targetsRedirect =
        policy::redirect_ipv4(message->name, message->namelen, redirectedAddress);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        clear_bytes(bytesSent);
        return policy::deny_socket_call();
    }
    WSAMSG redirectedMessage = *message;
    redirectedMessage.name = reinterpret_cast<sockaddr*>(&redirectedAddress);
    redirectedMessage.namelen = static_cast<INT>(sizeof(redirectedAddress));
    return call(socket, &redirectedMessage, flags, bytesSent, overlapped, completion);
}

} // namespace dawn::client::hooks::egress::winsock::transmission
