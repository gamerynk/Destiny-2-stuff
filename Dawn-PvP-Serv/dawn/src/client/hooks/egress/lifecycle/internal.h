#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../hooking/detour.h"
#include "../internal.h"

namespace dawn::client::hooks::egress::lifecycle {

enum class ModuleSlot : std::size_t {
    winsock,
    extensions,
    dns,
    count,
};

inline constexpr std::size_t kModuleCount = static_cast<std::size_t>(ModuleSlot::count);

inline constexpr std::size_t kRequiredHookCount = static_cast<std::size_t>(HookSlot::getAddrInfoA);

[[nodiscard]] bool load_modules() noexcept;

void release_modules() noexcept;

[[nodiscard]] HMODULE module_handle(ModuleSlot module) noexcept;

[[nodiscard]] bool resolve_specs(std::span<hooking::detour::Spec, kHookCount> specs,
                                 std::span<const char*, kHookCount> names,
                                 std::span<bool, kHookCount> resolved,
                                 std::size_t& count) noexcept;

} // namespace dawn::client::hooks::egress::lifecycle
