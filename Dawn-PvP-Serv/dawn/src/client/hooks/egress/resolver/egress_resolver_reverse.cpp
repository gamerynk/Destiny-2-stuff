#include "../internal.h"
#include "../policy/policy.h"
#include "redirect.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::resolver {
namespace {

[[nodiscard]] sockaddr_in redirect_endpoint(const SOCKADDR* address,
                                            socklen_t addressLength) noexcept {
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    if (address != nullptr && addressLength >= static_cast<socklen_t>(sizeof(endpoint))
        && address->sa_family == AF_INET) {
        endpoint.sin_port = reinterpret_cast<const sockaddr_in*>(address)->sin_port;
    }
    endpoint.sin_addr = redirect_address();
    return endpoint;
}

template <typename Character> void clear_name(Character* value, DWORD capacity) noexcept {
    if (value != nullptr && capacity != 0) {
        value[0] = Character{};
    }
}

} // namespace

hostent* WSAAPI host_by_address(const char* address, int length, int type) noexcept {
    (void)address;
    (void)length;
    const auto call = original<decltype(&::gethostbyaddr)>(HookSlot::getHostByAddress);
    if (call == nullptr) {
        policy::log_name_decision(policy::NameOperation::reverse, false);
        WSASetLastError(kBlockedHostError);
        return nullptr;
    }
    const in_addr redirect = redirect_address();
    policy::log_name_decision(policy::NameOperation::reverse, true);
    return call(reinterpret_cast<const char*>(&redirect),
                static_cast<int>(sizeof(redirect)),
                type == AF_INET6 ? AF_INET : type);
}

INT WSAAPI name_info_a(const SOCKADDR* address,
                       socklen_t addressLength,
                       PCHAR node,
                       DWORD nodeSize,
                       PCHAR service,
                       DWORD serviceSize,
                       INT flags) noexcept {
    clear_name(node, nodeSize);
    clear_name(service, serviceSize);
    const auto call = original<decltype(&::getnameinfo)>(HookSlot::getNameInfoA);
    if (call == nullptr) {
        return deny_resolution<char>(policy::NameOperation::reverse, nullptr, 0);
    }
    const sockaddr_in redirect = redirect_endpoint(address, addressLength);
    policy::log_name_decision(policy::NameOperation::reverse, true);
    return call(reinterpret_cast<const sockaddr*>(&redirect),
                static_cast<socklen_t>(sizeof(redirect)),
                node,
                nodeSize,
                service,
                serviceSize,
                flags);
}

INT WSAAPI name_info_w(const SOCKADDR* address,
                       socklen_t addressLength,
                       PWCHAR node,
                       DWORD nodeSize,
                       PWCHAR service,
                       DWORD serviceSize,
                       INT flags) noexcept {
    clear_name(node, nodeSize);
    clear_name(service, serviceSize);
    const auto call = original<decltype(&::GetNameInfoW)>(HookSlot::getNameInfoW);
    if (call == nullptr) {
        return deny_resolution<char>(policy::NameOperation::reverse, nullptr, 0);
    }
    const sockaddr_in redirect = redirect_endpoint(address, addressLength);
    policy::log_name_decision(policy::NameOperation::reverse, true);
    return call(reinterpret_cast<const sockaddr*>(&redirect),
                static_cast<socklen_t>(sizeof(redirect)),
                node,
                nodeSize,
                service,
                serviceSize,
                flags);
}

} // namespace dawn::client::hooks::egress::resolver
