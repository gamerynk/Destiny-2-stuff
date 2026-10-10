#include "egress_control_replacement.h"

#include <string_view>

#include "../../../../../core/logging/log.h"
#include "../../internal.h"
#include "../../policy/policy.h"

namespace dawn::client::hooks::egress::winsock::control {
namespace {

// extension pointers (ConnectEx, WSASendMsg, WSARecvMsg) have no loopback equivalent and
// no detour covers them; forward and report, the real fix is replacing those entry points
constexpr DWORD kExtensionPointerControl = SIO_GET_EXTENSION_FUNCTION_POINTER;

} // namespace

int WSAAPI socket_control(SOCKET socket,
                          DWORD controlCode,
                          LPVOID input,
                          DWORD inputSize,
                          LPVOID output,
                          DWORD outputSize,
                          LPDWORD bytesReturned,
                          LPWSAOVERLAPPED overlapped,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSAIoctl)>(HookSlot::wsaIoctl);
    if (call == nullptr) {
        if (bytesReturned != nullptr) {
            *bytesReturned = 0;
        }
        policy::log_name_decision(
            policy::NameOperation::control, false, std::string_view{}, controlCode);
        return policy::deny_socket_call();
    }
    if (controlCode == kExtensionPointerControl) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=egress stage=control target=unguarded action=allow result=ok");
    }
    policy::log_name_decision(
        policy::NameOperation::control, true, std::string_view{}, controlCode);
    return call(socket,
                controlCode,
                input,
                inputSize,
                output,
                outputSize,
                bytesReturned,
                overlapped,
                completion);
}

} // namespace dawn::client::hooks::egress::winsock::control
