#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::datagen::social_roster {

// build 86657 member +0x0A: signed activity definition index consumed by 14BEE10
constexpr std::size_t kActivityDefinitionOffset = 0x0AU;
constexpr std::size_t kPowerScalarOffset = 20;
constexpr std::size_t kEmblemDefinitionOffset = 36;
constexpr std::size_t kEmblemVariantOffset = 38;
constexpr std::size_t kSelectedCharacterSoidOffset = 0x50U;
// capacity used when the evaluator finds no matching current-party entry
constexpr std::size_t kJoinCapacityExternalOffset = 0x38U;
// capacity used by the evaluator's matching-current-entry branch
constexpr std::size_t kJoinCapacityMatchedOffset = 0x39U;
// capacity read by the evaluator's independent final bit-zero check
constexpr std::size_t kJoinCapacityOffset = 0x3AU;
// adjacent native policy bytes that must remain open
constexpr std::size_t kJoinPrivacyOffset = 0x3BU;
constexpr std::size_t kJoinClosedReasonsOffset = 0x3CU;
// build 86657 member +0x40 becomes contact +0x60: the presentation grouping key
constexpr std::size_t kPresentationGroupKeyOffset = 0x40U;
constexpr std::int32_t kUnassignedPresentationGroupKey = -1;
constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFFU;

struct Member final {
    std::uint64_t accountSoid{};
    std::int32_t power{};
    std::uint16_t emblemDefinition{kEmptyDefinitionIndex};
    // the working orbit/default record is index zero; never substitute absent (-1)
    std::int16_t activityIndex{};
};

[[nodiscard]] bool encode_directory(std::uint64_t accountSoid,
                                    std::uint64_t selectedCharacterSoid,
                                    std::span<std::byte> destination) noexcept;

[[nodiscard]] bool encode_member(const Member& member,
                                 std::span<std::byte> destination) noexcept;

} // namespace dawn::middleware::datagen::social_roster
