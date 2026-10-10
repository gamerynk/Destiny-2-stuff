#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <span>

#include "../../detour.h"

namespace dawn::client::hooking::detour::transaction {

constexpr std::size_t kThreadLimit = 1024;

struct Threads {
    std::array<HANDLE, kThreadLimit> handles{};
    std::array<DWORD, kThreadLimit> ids{};
    std::size_t count{};
};

struct Failure {
    const char* stage{"none"};
    LONG error{};
    std::size_t threadCount{};
    DWORD threadId{};
};
[[nodiscard]] Failure last_failure() noexcept;

enum class InspectionResult {
    clear,
    protectedCodeActive,
    failed,
};

[[nodiscard]] bool begin(Threads& threads) noexcept;

[[nodiscard]] bool abort(Threads& threads) noexcept;

[[nodiscard]] bool commit(Threads& threads) noexcept;

[[nodiscard]] InspectionResult inspect(const Threads& threads,
                                       std::span<const ProtectedCodeEntry> entries) noexcept;

} // namespace dawn::client::hooking::detour::transaction
