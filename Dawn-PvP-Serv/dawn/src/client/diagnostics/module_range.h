#pragma once

#include <Windows.h>

#include <cstdint>

namespace dawn::client::diagnostics {

struct ModuleRange {
    std::uintptr_t base{};
    std::uintptr_t end{};
};

[[nodiscard]] bool module_range(HMODULE module, ModuleRange& output) noexcept;

[[nodiscard]] bool contains(const ModuleRange& range, std::uintptr_t address) noexcept;

} // namespace dawn::client::diagnostics
