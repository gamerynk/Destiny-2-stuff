#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::targets::game::packages {

inline constexpr std::size_t kBlockKeySize = 16;
inline constexpr std::size_t kNonceSize = 12;

// installed key table read by the package block decoder; the bytes are proprietary, read at
// use time and never copied into State, cache, capture or logs
struct KeyTable {
    std::array<std::byte, kBlockKeySize> alternateKey{};
    std::array<std::byte, kBlockKeySize> identityConstant{};
    std::array<std::byte, kNonceSize> nonceBase{};
};

static_assert(sizeof(KeyTable) == 2 * kBlockKeySize + kNonceSize);

struct Targets {
    std::byte* keyTable{};
};

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

[[nodiscard]] bool is_resolved() noexcept;

[[nodiscard]] bool read(KeyTable& table) noexcept;

} // namespace dawn::client::targets::game::packages
