#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::state::build_data::material_requirements {

inline constexpr std::size_t kDefinitionCapacity = 512;
inline constexpr std::size_t kRequirementCapacity = 6;
inline constexpr std::uint16_t kUnavailableItemDefinitionIndex = 0xFFFFU;
inline constexpr std::uint16_t kUnavailableSetIndex = 0xFFFFU;
inline constexpr std::uint16_t kUnconditionalRequirement = 0xFFFFU;

struct Requirement {
    std::uint32_t quantity{};
    std::uint16_t itemDefinitionIndex{kUnavailableItemDefinitionIndex};
    std::uint16_t condition{kUnconditionalRequirement};
    bool deleteOnAction{};
    bool omitFromRequirements{};
};

struct Definition {
    std::uint32_t requirementSetHash{};
    std::uint16_t requirementSetIndex{kUnavailableSetIndex};
    std::uint8_t requirementCount{};
    std::array<Requirement, kRequirementCapacity> requirements{};
};

void clear() noexcept;
[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;
[[nodiscard]] bool find(std::uint16_t requirementSetIndex, Definition& definition) noexcept;
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;
[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::material_requirements
