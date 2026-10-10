#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::state::build_data::collectibles {

inline constexpr std::size_t kDefinitionCapacity = 1U << 15U;
inline constexpr std::uint16_t kUnavailableItemDefinitionIndex = 0xFFFFU;
inline constexpr std::uint16_t kUnavailableMaterialRequirementSetIndex = 0xFFFFU;
inline constexpr std::size_t kMaterialRequirementCapacity = 6;

struct MaterialRequirement {
    std::uint32_t quantity{};
    std::uint16_t itemDefinitionIndex{kUnavailableItemDefinitionIndex};
    bool deleteOnAction{};
    bool omitFromRequirements{};
};

struct Definition {
    std::uint32_t collectibleHash{};
    std::uint32_t materialRequirementSetHash{};
    std::uint16_t collectibleIndex{};
    std::uint16_t itemDefinitionIndex{kUnavailableItemDefinitionIndex};
    std::uint16_t materialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
    std::uint8_t materialRequirementCount{};
    std::array<MaterialRequirement, kMaterialRequirementCapacity> materialRequirements{};
};

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool find(std::uint16_t collectibleIndex, Definition& definition) noexcept;

[[nodiscard]] bool grants_item(std::uint16_t itemDefinitionIndex) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::collectibles
