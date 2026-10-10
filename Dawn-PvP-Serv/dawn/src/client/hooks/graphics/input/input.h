#pragma once

#include <Windows.h>

#include <cstdint>

namespace dawn::client::hooks::graphics::input {

// WM_USER + 13 makes the game run its presentation call right away
inline constexpr UINT kRequiredForwardMessage = WM_USER + 13;

[[nodiscard]] bool install(HWND window) noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool active(HWND window) noexcept;
struct MainInputSnapshot {
    HWND window{};
    DWORD thread{};
    std::uint64_t bindingGeneration{};
    std::uint64_t revision{};
    bool installed{};
};
[[nodiscard]] MainInputSnapshot snapshot_main_input() noexcept;

// the game creates this window during startup, so the first try can find nothing and a
// later frame retries
[[nodiscard]] bool install_raw_input_window() noexcept;

[[nodiscard]] bool uninstall_raw_input_window() noexcept;

struct RawCaptureClaim {
    std::uint64_t identity{};
    std::uint64_t visibilityGeneration{};
    std::uint64_t bindingGeneration{};
    HWND window{};
    bool operator==(const RawCaptureClaim&) const = default;
};

struct RawInputSnapshot {
    std::uint64_t visibilityGeneration{};
    std::uint64_t bindingGeneration{};
    HWND window{};
    unsigned int activeCallbacks{};
    RawCaptureClaim claim{};
    bool installed{};
    bool visible{};
    bool stableVisibility{};
    bool claimMatched{};
    bool drained{};
};

[[nodiscard]] RawInputSnapshot snapshot_raw_input() noexcept;

[[nodiscard]] bool claim_raw_input_capture(std::uint64_t visibilityGeneration,
    std::uint64_t bindingGeneration, RawCaptureClaim& claim) noexcept;

// removes only this exact claim; never clears a replacement after close/reopen/rebind
[[nodiscard]] bool release_raw_input_capture(const RawCaptureClaim& claim) noexcept;

struct RawOpeningLease {
    std::uint64_t identity{};
    std::uint64_t hiddenGeneration{};
    std::uint64_t bindingGeneration{};
    HWND window{};
    bool operator==(const RawOpeningLease&) const = default;
};
struct RawOpeningSnapshot {
    RawOpeningLease lease{};
    unsigned activeCallbacks{};
    bool matched{};
    bool drained{};
    bool retired{};
};
[[nodiscard]] bool claim_raw_opening(std::uint64_t hiddenGeneration,
    std::uint64_t bindingGeneration, RawOpeningLease& lease) noexcept;
[[nodiscard]] RawOpeningSnapshot snapshot_raw_opening(const RawOpeningLease& lease) noexcept;
[[nodiscard]] bool release_raw_opening(const RawOpeningLease& lease) noexcept;

} // namespace dawn::client::hooks::graphics::input
