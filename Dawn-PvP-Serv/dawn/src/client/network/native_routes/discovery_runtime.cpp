#include <Windows.h>

#include <array>
#include <cstring>
#include <span>
#include <string_view>

#include "discovery_data.h"
#include "runtime.h"

namespace dawn::client::network::native_routes {
namespace {
namespace data = hooks::egress::native_routes;
struct Page { void* address{}; DWORD protection{}; };

bool readable(const void* address, std::size_t size) noexcept {
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    const auto end = cursor + size;
    if (end < cursor) return false;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof memory) != sizeof memory
            || memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE
            || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}
}

bool publish_discovery(HMODULE game,
    const core::settings::client::external::Settings& external) noexcept {
    // numeric literals keep native UDP transport, DNS-free lookup and its timeout/retry logic;
    // a remapped discovery port needs a native port producer, which this variant refuses
    if (game == nullptr || !external.enabled
        || external.portRouteCount > external.portRoutes.size()) return false;
    for (std::size_t index = 0; index < external.portRouteCount; ++index) {
        const auto& route = external.portRoutes[index];
        if ((route.sourcePort == 3074 || route.sourcePort == 3075)
            && (route.address != external.address || route.targetPort != route.sourcePort))
            return false;
    }
    const auto hostEnd = std::find(external.host.begin(), external.host.end(), '\0');
    if (hostEnd == external.host.end()) return false;
    const std::string_view host(external.host.data(), hostEnd - external.host.begin());
    std::array<unsigned char, 4> address{};
    if (!core::settings::address::parse_ipv4(host, address) || address != external.address
        || address[0] == 0 || address[0] >= 224) return false;

    auto* base = reinterpret_cast<char*>(game);
    if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
        return false;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    if (!readable(sections, nt->FileHeader.NumberOfSections * sizeof(*sections))) return false;
    std::span<char> image{};
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        if (std::memcmp(section.Name, ".rdata\0\0", IMAGE_SIZEOF_SHORT_NAME) != 0) continue;
        const auto size = section.Misc.VirtualSize;
        if (section.VirtualAddress > nt->OptionalHeader.SizeOfImage
            || size > nt->OptionalHeader.SizeOfImage - section.VirtualAddress
            || !readable(base + section.VirtualAddress, size)) return false;
        image = {base + section.VirtualAddress, size};
        break;
    }
    data::Plan plan{};
    if (!data::prepare(image, host, plan)) return false;

    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (system.dwPageSize == 0) return false;
    std::array<Page, data::kDiscoveryHosts.size() * 2> pages{};
    std::size_t pageCount{};
    for (std::size_t index = 0; index < plan.offsets.size(); ++index) {
        const auto first = reinterpret_cast<std::uintptr_t>(image.data() + plan.offsets[index]);
        const auto last = first + data::kDiscoveryHosts[index].size();
        for (auto page = first - first % system.dwPageSize; page <= last;
             page += system.dwPageSize) {
            bool duplicate = false;
            for (std::size_t slot = 0; slot < pageCount; ++slot)
                duplicate |= pages[slot].address == reinterpret_cast<void*>(page);
            if (!duplicate) pages[pageCount++].address = reinterpret_cast<void*>(page);
        }
    }
    std::size_t writable{};
    while (writable < pageCount) {
        auto& page = pages[writable];
        if (VirtualProtect(page.address, system.dwPageSize, PAGE_READWRITE,
                           &page.protection) == FALSE) break;
        ++writable;
    }
    const bool changed = writable == pageCount && data::apply(image, host, plan);
    bool restored = true;
    while (writable != 0) {
        auto& page = pages[--writable];
        DWORD discarded{};
        restored &= VirtualProtect(page.address, system.dwPageSize,
                                   page.protection, &discarded) != FALSE;
    }
    return changed && restored;
}
} // namespace dawn::client::network::native_routes
