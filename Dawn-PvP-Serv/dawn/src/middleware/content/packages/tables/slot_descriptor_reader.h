#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::content::packages::tables {

inline constexpr std::uint32_t kPlacedObjectClass = 0x80809C36U;
// a descriptor repeats this at its own offset 8
inline constexpr std::uint32_t kDescriptorMark = 0x70U;
inline constexpr std::size_t kDescriptorSize = 128;
inline constexpr std::size_t kDescriptorStep = 4;
inline constexpr std::uint32_t kClassHighHalf = 0x8080U;
// slot types with no auth or sense schema record this instead
inline constexpr std::uint32_t kAbsentSchema = 0xFFFFFFFFU;
inline constexpr std::uint32_t kSlotIndirectClass = 0x80809468U;
inline constexpr std::uint32_t kSlotRedirectClass = 0x80809B14U;
inline constexpr std::size_t kSlotIndirectDescriptor = 16;
inline constexpr std::size_t kSlotRedirectTagOffset = 12;

inline constexpr std::size_t kDescriptorOwnTagOffset = 0;
inline constexpr std::size_t kDescriptorComponentClassOffset = 4;
inline constexpr std::size_t kDescriptorMarkOffset = 8;
inline constexpr std::size_t kDescriptorRegistryKeyOffset = 48;
inline constexpr std::size_t kDescriptorSlotTypeOffset = 52;
inline constexpr std::size_t kDescriptorSlotIndexOffset = 54;
inline constexpr std::size_t kDescriptorSenseSchemaOffset = 68;
inline constexpr std::size_t kDescriptorAuthSchemaOffset = 72;

struct SlotDescriptor {
    std::uint32_t componentClass{};
    std::uint32_t senseSchema{};
    std::uint32_t authSchema{};
    std::uint16_t slotType{};
    std::uint16_t slotIndex{};
};

using DescriptorVisitor = bool (*)(void* context, const SlotDescriptor& descriptor) noexcept;

[[nodiscard]] constexpr bool is_class_id(std::uint32_t value) noexcept {
    return (value >> 16U) == kClassHighHalf;
}

[[nodiscard]] constexpr bool is_schema_id(std::uint32_t value) noexcept {
    return value == kAbsentSchema || is_class_id(value);
}

// four predicates must hold together: own tag repeated, the mark, owner's registry key,
// class ids throughout. dropping the last admits 10x the noise
[[nodiscard]] bool visit_slot_descriptors(std::span<const std::byte> blob,
                                          std::uint32_t ownTag,
                                          std::uint32_t registryKey,
                                          DescriptorVisitor visitor,
                                          void* context) noexcept;

// redirect nodes carry one child, indirect nodes an array; following only the first
// indirect child loses component schemas on later branches
[[nodiscard]] bool descriptor_child_tags(std::span<const std::byte> blob,
                                         std::uint32_t classId,
                                         std::span<std::uint32_t> output,
                                         std::size_t& count) noexcept;

[[nodiscard]] bool next_descriptor_tag(std::span<const std::byte> blob,
                                       std::uint32_t classId,
                                       std::uint32_t& tag) noexcept;

} // namespace dawn::middleware::content::packages::tables
