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

[[nodiscard]] std::size_t submitted_bytes(LPWSABUF buffers, DWORD count) noexcept {
    std::size_t total = 0;
    if (buffers != nullptr && count <= 64U) {
        for (DWORD index = 0; index < count; ++index) {
            total += buffers[index].len;
        }
    }
    return total;
}

} // namespace

int WSAAPI send_bytes(SOCKET socket, const char* buffer, int length, int flags) noexcept {
    const auto call = original<decltype(&::send)>(HookSlot::send);
    if (call != nullptr && policy::is_native_sdr_udp_socket(socket)
        && policy::has_native_sdr_ipv4_peer(socket)
        && policy::allow_native_sdr_passthrough(_ReturnAddress())) {
        const int result = call(socket, buffer, length, flags);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::send, true,
                                     length > 0 ? static_cast<std::size_t>(length) : 0U,
                                     result, error);
        WSASetLastError(error);
        return result;
    }
    const bool targetsRedirect = policy::has_redirect_target_peer(socket);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    return call(socket, buffer, length, flags);
}

int WSAAPI send_buffers(SOCKET socket,
                        LPWSABUF buffers,
                        DWORD bufferCount,
                        LPDWORD bytesSent,
                        DWORD flags,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSASend)>(HookSlot::wsaSend);
    if (call != nullptr && policy::is_native_sdr_udp_socket(socket)
        && policy::has_native_sdr_ipv4_peer(socket)
        && policy::allow_native_sdr_passthrough(_ReturnAddress())) {
        const std::size_t submitted = submitted_bytes(buffers, bufferCount);
        const int result = call(socket, buffers, bufferCount, bytesSent,
                                flags, overlapped, completion);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::wsaSend, true,
                                     submitted, result, error);
        WSASetLastError(error);
        return result;
    }
    const bool targetsRedirect = policy::has_redirect_target_peer(socket);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        clear_bytes(bytesSent);
        return policy::deny_socket_call();
    }
    return call(socket, buffers, bufferCount, bytesSent, flags, overlapped, completion);
}

} // namespace dawn::client::hooks::egress::winsock::transmission
