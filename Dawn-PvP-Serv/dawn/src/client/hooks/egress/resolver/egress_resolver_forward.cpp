#include <string_view>

#include "../internal.h"
#include "../policy/policy.h"
#include "redirect.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::resolver {
namespace {

[[nodiscard]] int redirect_family(int family) noexcept {
    return family == AF_INET6 ? AF_INET6 : AF_INET;
}

template <typename AddressInfo>
[[nodiscard]] AddressInfo redirect_hints(const AddressInfo* hints, int family) noexcept {
    AddressInfo forwarded{};
    forwarded.ai_flags = AI_NUMERICHOST;
    forwarded.ai_family = family;
    if (hints != nullptr) {
        forwarded.ai_flags |= hints->ai_flags;
        forwarded.ai_socktype = hints->ai_socktype;
        forwarded.ai_protocol = hints->ai_protocol;
    }
    return forwarded;
}

} // namespace

INT WSAAPI address_info_a(PCSTR node,
                          PCSTR service,
                          const ADDRINFOA* hints,
                          PADDRINFOA* result) noexcept {
    if (result != nullptr) {
        *result = nullptr;
    }
    const int family = redirect_family(hints != nullptr ? hints->ai_family : AF_UNSPEC);
    const auto call = original<decltype(&::getaddrinfo)>(HookSlot::getAddrInfoA);
    if (node == nullptr || node[0] == '\0' || result == nullptr || call == nullptr) {
        return deny_resolution(policy::NameOperation::resolve, node, family);
    }
    policy::log_name_decision(policy::NameOperation::resolve, true, node, family);
    const ADDRINFOA forwarded = redirect_hints(hints, family);
    return call(
        family == AF_INET6 ? kLoopbackHost6A : redirect_host_a(), service, &forwarded, result);
}

INT WSAAPI address_info_w(PCWSTR node,
                          PCWSTR service,
                          const ADDRINFOW* hints,
                          PADDRINFOW* result) noexcept {
    if (result != nullptr) {
        *result = nullptr;
    }
    const int family = redirect_family(hints != nullptr ? hints->ai_family : AF_UNSPEC);
    const auto call = original<decltype(&::GetAddrInfoW)>(HookSlot::getAddrInfoW);
    if (node == nullptr || node[0] == L'\0' || result == nullptr || call == nullptr) {
        return deny_resolution(policy::NameOperation::resolve, node, family);
    }
    policy::log_name_decision(policy::NameOperation::resolve, true, node, family);
    const ADDRINFOW forwarded = redirect_hints(hints, family);
    return call(
        family == AF_INET6 ? kLoopbackHost6W : redirect_host_w(), service, &forwarded, result);
}

INT WSAAPI address_info_ex_w(PCWSTR name,
                             PCWSTR service,
                             DWORD nameSpace,
                             LPGUID provider,
                             const ADDRINFOEXW* hints,
                             PADDRINFOEXW* result,
                             timeval* timeout,
                             LPOVERLAPPED overlapped,
                             LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
                             LPHANDLE nameHandle) noexcept {
    if (result != nullptr) {
        *result = nullptr;
    }
    if (nameHandle != nullptr) {
        *nameHandle = nullptr;
    }
    const auto call = original<decltype(&::GetAddrInfoExW)>(HookSlot::getAddrInfoExW);
    if (name == nullptr || name[0] == 0 || result == nullptr || call == nullptr) {
        return deny_resolution(policy::NameOperation::resolve, name, 0);
    }
    policy::log_name_decision(policy::NameOperation::resolve, true, name, 0);
    return call(redirect_host_w(),
                service,
                nameSpace,
                provider,
                hints,
                result,
                timeout,
                overlapped,
                completion,
                nameHandle);
}

hostent* WSAAPI host_by_name(const char* name) noexcept {
    if (name == nullptr || name[0] == '\0') {
        // a null name must not reach the string_view parameter; the conversion runs strlen
        policy::log_name_decision(policy::NameOperation::resolve,
                                  false,
                                  name != nullptr ? std::string_view(name) : std::string_view{},
                                  AF_INET);
        WSASetLastError(kBlockedHostError);
        return nullptr;
    }
    // Winsock keeps results in per-thread storage that callers read before their next lookup
    thread_local in_addr address{};
    thread_local char* addressList[2]{};
    thread_local char* aliases[1]{};
    thread_local hostent entry{};
    address = redirect_address();
    addressList[0] = reinterpret_cast<char*>(&address);
    addressList[1] = nullptr;
    aliases[0] = nullptr;
    entry.h_name = const_cast<char*>(name);
    entry.h_aliases = aliases;
    entry.h_addrtype = AF_INET;
    entry.h_length = static_cast<short>(sizeof(address));
    entry.h_addr_list = addressList;
    policy::log_name_decision(policy::NameOperation::resolve, true, name, AF_INET);
    return &entry;
}

} // namespace dawn::client::hooks::egress::resolver
