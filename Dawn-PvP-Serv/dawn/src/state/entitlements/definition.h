#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::state::entitlements {

// the client allocates manifest entitlement handles upward from this base
inline constexpr std::uint32_t kHandleBase = 0xE0200001;
// every ContentConfig package row uses this one offer key
inline constexpr std::uint32_t kOfferKey = 0xD0000448;
inline constexpr std::size_t kNameCapacity = 32;
inline constexpr std::size_t kCapacity = 128;

enum class Ownership : std::uint8_t {
    none,
    handle,
    application,
};

struct Entitlement final {
    std::array<char, kNameCapacity> name{};
    std::uint8_t nameLength{};
    Ownership ownership{Ownership::none};
};

struct Table final {
    std::array<Entitlement, kCapacity> entries{};
    std::size_t count{};
};

[[nodiscard]] constexpr std::uint32_t handle(std::size_t index) noexcept {
    return kHandleBase + static_cast<std::uint32_t>(index);
}

[[nodiscard]] constexpr std::string_view name_of(const Entitlement& entry) noexcept {
    return {entry.name.data(), entry.nameLength};
}

[[nodiscard]] constexpr std::span<const Entitlement> used(const Table& table) noexcept {
    return std::span(table.entries).first(table.count);
}

} // namespace dawn::state::entitlements
