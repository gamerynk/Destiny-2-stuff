#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../hooking/detour.h"

namespace dawn::client::hooks::polled_input {

inline constexpr SHORT kKeyReleased = 0;
inline constexpr SHORT kKeyHeld = static_cast<SHORT>(0x8000);
inline constexpr std::uint32_t kNoForcedKey = 0;

using GetKeyState = SHORT(WINAPI*)(int);
using GetAsyncKeyState = SHORT(WINAPI*)(int);

enum class HookSlot : std::size_t {
    getKeyState,
    getAsyncKeyState,
    count,
};

inline constexpr std::size_t kHandleCount = static_cast<std::size_t>(HookSlot::count);

extern std::array<hooking::detour::Handle, kHandleCount> g_handles;
extern std::array<void*, kHandleCount> g_targets;

template <typename Function> [[nodiscard]] Function original(HookSlot slot) noexcept {
    const auto index = static_cast<std::size_t>(slot);
    void* entry = g_handles[index].original;
    if (entry == nullptr) {
        entry = g_targets[index];
    }
    return reinterpret_cast<Function>(entry);
}

SHORT WINAPI get_key_state(int virtualKey) noexcept;

SHORT WINAPI get_async_key_state(int virtualKey) noexcept;

// an empty range contains nothing, so the guard forwards every call instead of blocking
[[nodiscard]] bool resolve_game_range() noexcept;

void clear_game_range() noexcept;

void apply_policy(bool visible) noexcept;

} // namespace dawn::client::hooks::polled_input
