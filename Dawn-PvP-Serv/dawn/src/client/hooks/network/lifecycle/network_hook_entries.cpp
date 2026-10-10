#include "network_hook_entries.h"

#include "../../../targets/steam_targets.h"
#include "../coordinator/network_call_coordinator.h"

namespace dawn::client::hooks::network::lifecycle {
namespace {

[[nodiscard]] std::array<void*, kPlatformSlots.size()> platform_replacements() noexcept {
    return {
        platform::authentication_status_entry_point(),
        platform::set_certificate_entry_point(),
    };
}

} // namespace

PlatformSpecs platform_specs() noexcept {
    const auto replacements = platform_replacements();
    const targets::steam::Targets& resolved = targets::steam::get();
    return {
        hooking::detour::Spec{resolved.authenticationStatus, replacements[0]},
        hooking::detour::Spec{resolved.setCertificate, replacements[1]},
    };
}

PlatformProtectedEntries platform_protected_entries() noexcept {
    const auto replacements = platform_replacements();
    PlatformProtectedEntries entries{};
    for (std::size_t index = 0; index < replacements.size(); ++index) {
        entries[index].address = replacements[index];
    }
    entries[replacements.size()].address = coordinator::ingress_entry_point();
    entries[replacements.size() + 1].address = coordinator::egress_entry_point();
    return entries;
}

} // namespace dawn::client::hooks::network::lifecycle
