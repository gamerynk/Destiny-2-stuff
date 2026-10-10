#include "module_range.h"

#include <cstddef>

namespace dawn::client::diagnostics {

bool module_range(HMODULE module, ModuleRange& output) noexcept {
    if (module == nullptr) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::byte*>(module);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    output = ModuleRange{base, base + headers->OptionalHeader.SizeOfImage};
    return true;
}

bool contains(const ModuleRange& range, std::uintptr_t address) noexcept {
    return range.end != 0 && address >= range.base && address < range.end;
}

} // namespace dawn::client::diagnostics
