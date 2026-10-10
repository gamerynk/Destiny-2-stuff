#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::state::build_data::items {

inline constexpr std::size_t kDefinitionCapacity = 32768;
inline constexpr std::uint8_t kUnresolvedBucketId = 0xFF;
inline constexpr std::uint16_t kUnavailableMaterialRequirementSetIndex = 0xFFFFU;

struct Definition {
    std::uint32_t definitionHash{};
    std::uint16_t definitionIndex{};
    std::uint8_t bucketId{kUnresolvedBucketId};
    std::uint16_t insertionMaterialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
    std::uint16_t enabledMaterialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
};

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool
find(std::uint32_t definitionHash, std::uint8_t bucketId, Definition& definition) noexcept;

[[nodiscard]] bool find_hash(std::uint32_t definitionHash, Definition& definition) noexcept;

[[nodiscard]] bool find_index(std::uint16_t definitionIndex, Definition& definition) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::items
