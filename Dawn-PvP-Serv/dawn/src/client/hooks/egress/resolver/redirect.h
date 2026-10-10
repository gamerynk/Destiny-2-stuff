#pragma once

#include <array>
#include <string_view>

#include "../../../../core/settings/settings.h"
#include "../platform/sdk.h"
#include "../policy/policy.h"

namespace dawn::client::hooks::egress::resolver {

// a non-recoverable failure stops callers retrying another server
inline constexpr int kBlockedResolutionError = EAI_FAIL;
inline constexpr int kBlockedHostError = WSAHOST_NOT_FOUND;
inline constexpr char kLoopbackHost6A[] = "::1";
inline constexpr wchar_t kLoopbackHost6W[] = L"::1";

template <typename Character>
[[nodiscard]] inline int deny_resolution(policy::NameOperation operation,
                                         const Character* name,
                                         unsigned selector) noexcept {
    policy::log_name_decision(operation,
                              false,
                              name != nullptr ? std::basic_string_view<Character>(name)
                                              : std::basic_string_view<Character>{},
                              selector);
    WSASetLastError(kBlockedResolutionError);
    return kBlockedResolutionError;
}

[[nodiscard]] inline const core::settings::client::external::Settings&
redirect_settings() noexcept {
    static const core::settings::client::external::Settings kLoopback{};
    const core::settings::client::external::Settings& configured =
        core::settings::get().client.externalServer;
    return configured.enabled ? configured : kLoopback;
}

// numeric host so the original cannot emit DNS
[[nodiscard]] inline const char* redirect_host_a() noexcept {
    return redirect_settings().host.data();
}

[[nodiscard]] inline const wchar_t* redirect_host_w() noexcept {
    return redirect_settings().hostWide.data();
}

// the name-based connect APIs take a mutable node
using WideNode = std::array<wchar_t, core::settings::client::external::kHostCapacity>;

[[nodiscard]] inline WideNode redirect_node() noexcept {
    WideNode node{};
    const wchar_t* const host = redirect_host_w();
    for (std::size_t index = 0; index + 1 < node.size() && host[index] != L'\0'; ++index) {
        node[index] = host[index];
    }
    return node;
}

[[nodiscard]] inline in_addr redirect_address() noexcept {
    const std::array<unsigned char, 4> octets = policy::redirect_octets();
    in_addr value{};
    value.S_un.S_un_b = {octets[0], octets[1], octets[2], octets[3]};
    return value;
}

} // namespace dawn::client::hooks::egress::resolver
