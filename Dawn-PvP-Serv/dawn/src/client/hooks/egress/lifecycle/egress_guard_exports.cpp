#include "../dns/egress_dns_replacements.h"
#include "../extensions/egress_extension_replacements.h"
#include "../resolver/replacements.h"
#include "../winsock/replacements.h"
#include "internal.h"
#include "ansi_resolver_layer.h"

namespace dawn::client::hooks::egress::lifecycle {
namespace {

struct ExportDefinition {
    ModuleSlot module{};
    const char* name{};
    void* replacement{};
};

template <typename Function>
[[nodiscard]] ExportDefinition
export_definition(ModuleSlot module, const char* name, Function replacement) noexcept {
    return ExportDefinition{module, name, reinterpret_cast<void*>(replacement)};
}

} // namespace

bool resolve_specs(std::span<hooking::detour::Spec, kHookCount> specs,
                   std::span<const char*, kHookCount> names,
                   std::span<bool, kHookCount> resolved,
                   std::size_t& count) noexcept {
    count = 0;

    const std::array<ExportDefinition, kHookCount> exports{
        export_definition(ModuleSlot::winsock, "connect", &winsock::connection::connect_socket),
        export_definition(ModuleSlot::winsock, "send", &winsock::transmission::send_bytes),
        export_definition(ModuleSlot::winsock, "WSASend", &winsock::transmission::send_buffers),
        export_definition(ModuleSlot::winsock, "sendto", &winsock::transmission::send_bytes_to),
        export_definition(
            ModuleSlot::winsock, "WSASendTo", &winsock::transmission::send_buffers_to),
        export_definition(ModuleSlot::winsock, "WSASendMsg", &winsock::transmission::send_message),
        export_definition(ModuleSlot::extensions, "TransmitFile", &extensions::transmit_file),
        export_definition(ModuleSlot::winsock, "GetAddrInfoW", &resolver::address_info_w),
        export_definition(ModuleSlot::winsock, "GetAddrInfoExW", &resolver::address_info_ex_w),
        export_definition(ModuleSlot::winsock, "gethostbyname", &resolver::host_by_name),
        export_definition(ModuleSlot::winsock, "gethostbyaddr", &resolver::host_by_address),
        export_definition(ModuleSlot::winsock, "getnameinfo", &resolver::name_info_a),
        export_definition(ModuleSlot::winsock, "GetNameInfoW", &resolver::name_info_w),
        export_definition(ModuleSlot::dns, "DnsQueryEx", &dns::query_ex),
        export_definition(ModuleSlot::winsock, "recvfrom", &winsock::reception::receive_bytes_from),
        export_definition(ModuleSlot::winsock, "WSARecvFrom", &winsock::reception::receive_buffers_from),
        export_definition(ModuleSlot::winsock, "WSAIoctl", &winsock::control::socket_control),
        export_definition(ModuleSlot::winsock, "getaddrinfo", &resolver::address_info_a),
    };

    for (std::size_t index = 0; index < exports.size(); ++index) {
        const ExportDefinition& definition = exports[index];
        const HMODULE module = module_handle(definition.module);
        if (index == static_cast<std::size_t>(HookSlot::getAddrInfoA)
            && native_ansi_uses_wide_guard(module)) {
            names[index] = nullptr;
            resolved[index] = false;
            continue;
        }
        void* target = reinterpret_cast<void*>(GetProcAddress(module, definition.name));
        names[index] = definition.name;

        resolved[index] = target != nullptr;

        if (target == nullptr) {
            return false;
        }
        specs[count] = hooking::detour::Spec{target, definition.replacement};
        count = index + 1;
    }
    return count >= kRequiredHookCount;
}

} // namespace dawn::client::hooks::egress::lifecycle
