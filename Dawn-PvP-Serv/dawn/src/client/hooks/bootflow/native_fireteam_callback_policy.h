#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::client::hooks::bootflow::native_fireteam_callback_policy {

inline constexpr std::uintptr_t kFrameRva = 0x016D2320U;
inline constexpr std::uintptr_t kOrderedCallsRva = 0x016D2397U;
inline constexpr std::array<std::uint8_t, 38> kOrderedCalls{
    0xE8,0x64,0x23,0x4C,0xFF,0xE8,0x7F,0xC1,0x08,0x00,
    0xE8,0xEA,0xDE,0x08,0x00,0xE8,0x95,0xE4,0xFE,0xFF,
    0x48,0x8B,0xC8,0xE8,0x1D,0x25,0x13,0x00,
    0xE8,0x58,0x86,0x10,0x00,0xE8,0x13,0xFE,0x00,0x00};
inline constexpr std::array<std::uintptr_t, 7> kObservedChain{
    0x00448514U,0x016D23BDU,0x0031B004U,0x00318991U,
    0x0031B241U,0x003537B3U,0x00353874U};

[[nodiscard]] constexpr bool ordered_calls_match(std::span<const std::byte> code) noexcept {
    if (code.size() != kOrderedCalls.size()) return false;
    for (std::size_t index = 0; index < code.size(); ++index)
        if (std::to_integer<std::uint8_t>(code[index]) != kOrderedCalls[index]) return false;
    return true;
}

[[nodiscard]] constexpr bool owns_network_frame(std::span<const std::uintptr_t> frames,
                                                std::uintptr_t imageBase,
                                                std::size_t imageSize) noexcept {
    if (frames.empty() || frames.size() > 16 || imageBase == 0 || imageSize == 0) return false;
    std::size_t nativeCount = 0;
    for (const auto address : frames) {
        if (address < imageBase || address - imageBase >= imageSize) continue;
        if (nativeCount >= kObservedChain.size()
            || address - imageBase != kObservedChain[nativeCount++]) return false;
    }
    return nativeCount == kObservedChain.size();
}

struct CallbackDepth final {
    std::uint32_t depth{};
    bool captured{};
    void begin() noexcept { ++depth; }
    void end() noexcept { if (depth != 0 && --depth == 0) captured = false; }
    [[nodiscard]] bool is_outermost() const noexcept { return depth == 1; }
    [[nodiscard]] bool admit() noexcept {
        if (depth != 1 || captured) return false;
        captured = true;
        return true;
    }
};

} // namespace dawn::client::hooks::bootflow::native_fireteam_callback_policy
