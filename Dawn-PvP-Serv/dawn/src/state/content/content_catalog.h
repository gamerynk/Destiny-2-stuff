#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::state::content {

// names over 127 bytes are rejected, not truncated
inline constexpr std::size_t kDefinitionNameCapacity = 128;
inline constexpr std::size_t kDefinitionCatalogCapacity = 4096;

struct Definition {
    std::array<char, kDefinitionNameCapacity> name{};
    std::size_t nameLength{};
    std::uint32_t tag{};
    std::uint32_t classId{};
};

void clear() noexcept;

[[nodiscard]] bool seal() noexcept;

[[nodiscard]] bool insert(std::string_view name, std::uint32_t tag, std::uint32_t classId) noexcept;

[[nodiscard]] bool
lookup(std::string_view name, std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] bool
lookup_hash(std::uint32_t nameHash, std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::content
