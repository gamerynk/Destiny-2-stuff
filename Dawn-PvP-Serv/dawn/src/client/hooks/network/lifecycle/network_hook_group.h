#pragma once

#include <array>
#include <span>

#include "../platform.h"

namespace dawn::client::hooks::network::lifecycle {

inline constexpr std::array kPlatformSlots{
    HookSlot::authenticationStatus,
    HookSlot::setCertificate,
};

[[nodiscard]] inline std::span<const HookSlot> platform_slots() noexcept {
    return std::span<const HookSlot>(kPlatformSlots).first(
        (platform::native_authentication_requested() || platform::automatic_native_selected()) ? 0U
            : platform::native_certificate_requested() ? 1U : kPlatformSlots.size());
}

[[nodiscard]] bool all_installed(std::span<const HookSlot> slots) noexcept;

[[nodiscard]] bool any_installed(std::span<const HookSlot> slots) noexcept;

[[nodiscard]] bool all_accepting(std::span<const HookSlot> slots) noexcept;

void set_accepting(std::span<const HookSlot> slots, bool accepting) noexcept;

[[nodiscard]] bool install_group(std::span<const hooking::detour::Spec> specs,
                                 std::span<const HookSlot> slots) noexcept;

[[nodiscard]] bool
uninstall_group(std::span<const HookSlot> slots,
                std::span<const hooking::detour::ProtectedCodeEntry> protectedEntries) noexcept;

} // namespace dawn::client::hooks::network::lifecycle
