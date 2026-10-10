#include "egress_connection_replacements.h"

#include <intrin.h>

#include "../../internal.h"
#include "../../policy/policy.h"

namespace dawn::client::hooks::egress::winsock::connection {
int WSAAPI connect_socket(SOCKET socket, const sockaddr* name, int nameLength) noexcept {
    const auto call = original<decltype(&::connect)>(HookSlot::connect);
    if (call != nullptr && policy::is_native_sdr_udp_socket(socket)
        && policy::is_native_sdr_ipv4_target(name, nameLength)
        && policy::allow_native_sdr_passthrough(_ReturnAddress())) {
        const int result = call(socket, name, nameLength);
        const int error = WSAGetLastError();
        policy::report_native_sdr_io(policy::NativeSdrPath::connect, false, 0, result, error);
        WSASetLastError(error);
        return result;
    }
    sockaddr_in redirected{};
    const bool targetsRedirect = policy::redirect_ipv4(name, nameLength, redirected);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    return call(socket,
                reinterpret_cast<const sockaddr*>(&redirected),
                static_cast<int>(sizeof(redirected)));
}

} // namespace dawn::client::hooks::egress::winsock::connection
