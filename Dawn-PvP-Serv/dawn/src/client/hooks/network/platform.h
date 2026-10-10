#pragma once

#include <Windows.h>

#include <WinSock2.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../../hooking/detour.h"

namespace dawn::client::hooks::network {

enum class HookSlot : std::size_t {
    authenticationStatus,
    setCertificate,
    count,
};

inline constexpr std::size_t kHandleCount = static_cast<std::size_t>(HookSlot::count);

extern SRWLOCK g_lock;
extern std::array<hooking::detour::Handle, kHandleCount> g_handles;
extern std::array<void*, kHandleCount> g_targetEntries;
extern std::array<bool, kHandleCount> g_accepting;
extern volatile LONG g_activeCalls;

template <typename Function> [[nodiscard]] Function original(HookSlot slot) noexcept {
    const auto index = static_cast<std::size_t>(slot);
    void* entry = g_handles[index].original;
    if (entry == nullptr) {
        entry = g_targetEntries[index];
    }
    return reinterpret_cast<Function>(entry);
}

namespace platform {

[[nodiscard]] bool native_certificate_requested() noexcept;

[[nodiscard]] bool native_authentication_requested() noexcept;

[[nodiscard]] bool automatic_native_selected() noexcept;
void select_automatic_native(bool selected) noexcept;
[[nodiscard]] bool automatic_native_ready() noexcept;

[[nodiscard]] void* authentication_status_entry_point() noexcept;

[[nodiscard]] void* set_certificate_entry_point() noexcept;

} // namespace platform


} // namespace dawn::client::hooks::network
