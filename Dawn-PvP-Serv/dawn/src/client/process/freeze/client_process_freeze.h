#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

namespace dawn::client::process::freeze {

inline constexpr std::size_t kThreadLimit = 1024;

struct Held {
    std::array<HANDLE, kThreadLimit> handles{};
    std::array<DWORD, kThreadLimit> ids{};
    std::size_t count{};
    ULONG_PTR loaderCookie{};
    bool loaderLocked{};
    bool heapLocked{};
};

// the game can't time a connection out while its threads don't run, so slow work here no
// longer drops the session
[[nodiscard]] bool hold(Held& held) noexcept;

void release(Held& held) noexcept;

// Detours transactions suspend threads too; two suspenders at once stop each other,
// each waiting on a thread the other froze
void enter_exclusive() noexcept;

void leave_exclusive() noexcept;

} // namespace dawn::client::process::freeze
