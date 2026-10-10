#include "egress_reception_replacements.h"

#include <intrin.h>

#include "../../../../../core/logging/log.h"
#include "../../internal.h"
#include "../../policy/policy.h"

namespace dawn::client::hooks::egress::winsock::reception {
namespace {

constexpr std::string_view kReceiveDeny = "ev=egress stage=receive target=unverified result=deny";

[[nodiscard]] bool allow_receive(bool originalAvailable) noexcept {
    if (!originalAvailable) {
        core::log::write(core::log::Channel::client, core::log::Level::warn, kReceiveDeny);
    }
    return originalAvailable;
}

} // namespace

int WSAAPI receive_bytes_from(SOCKET socket,
                              char* buffer,
                              int length,
                              int flags,
                              sockaddr* source,
                              int* sourceLength) noexcept {
    const auto call = original<decltype(&::recvfrom)>(HookSlot::recvFrom);
    if (!allow_receive(call != nullptr)) {
        return policy::deny_socket_call();
    }
    const bool native = policy::is_native_sdr_udp_socket(socket)
                        && policy::allow_native_sdr_passthrough(_ReturnAddress());
    const int result = call(socket, buffer, length, flags, source, sourceLength);
    if (native) {
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::recvFrom, false,
                                     result > 0 ? static_cast<std::size_t>(result) : 0U,
                                     result, error);
        WSASetLastError(error);
    }
    return result;
}

int WSAAPI receive_buffers_from(SOCKET socket,
                                LPWSABUF buffers,
                                DWORD bufferCount,
                                LPDWORD bytesReceived,
                                LPDWORD flags,
                                sockaddr* source,
                                LPINT sourceLength,
                                LPWSAOVERLAPPED overlapped,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSARecvFrom)>(HookSlot::wsaRecvFrom);
    if (!allow_receive(call != nullptr)) {
        return policy::deny_socket_call();
    }
    const bool native = policy::is_native_sdr_udp_socket(socket)
                        && policy::allow_native_sdr_passthrough(_ReturnAddress());
    const int result = call(socket, buffers, bufferCount, bytesReceived, flags, source,
                            sourceLength, overlapped, completion);
    if (native) {
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::wsaRecvFrom, false,
                                     result == 0 && overlapped == nullptr
                                         && completion == nullptr && bytesReceived != nullptr
                                         ? static_cast<std::size_t>(*bytesReceived) : 0U,
                                     result, error);
        WSASetLastError(error);
    }
    return result;
}

} // namespace dawn::client::hooks::egress::winsock::reception
