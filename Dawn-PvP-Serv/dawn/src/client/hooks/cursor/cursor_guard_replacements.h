#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

#include "../../hooking/detour.h"

namespace dawn::client::hooks::cursor {

inline constexpr BOOL kCallSucceeded = TRUE;

using SetCursorPos = BOOL(WINAPI*)(int, int);
using ClipCursor = BOOL(WINAPI*)(const RECT*);

enum class HookSlot : std::size_t {
    setCursorPos,
    clipCursor,
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

BOOL WINAPI set_cursor_pos(int x, int y) noexcept;

BOOL WINAPI clip_cursor(const RECT* bounds) noexcept;

void apply_policy(bool visible) noexcept;
void publish_guarded_visibility(bool visible) noexcept;
void restore_guarded_policy(bool visible) noexcept;

} // namespace dawn::client::hooks::cursor
