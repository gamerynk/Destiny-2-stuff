#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::middleware::datagen::family4::inventory::layout {

inline constexpr std::size_t kDefinitionPaddingSize = 6;
inline constexpr std::size_t kTailPaddingSize = 4;
inline constexpr std::size_t kEntrySize = 32;
inline constexpr std::size_t kDefinitionIndexOffset = 0;
inline constexpr std::size_t kInstanceSoidOffset = 8;
inline constexpr std::size_t kQuantityOffset = 16;
inline constexpr std::size_t kMutationSerialOffset = 20;
inline constexpr std::size_t kFlagsOffset = 24;

#pragma pack(push, 1)

struct Entry {
    std::uint16_t definitionIndex{};
    std::array<std::byte, kDefinitionPaddingSize> definitionPadding{};
    std::uint64_t instanceSoid{};
    std::int32_t quantity{};
    // rising serial; keeps inventory mutation and eviction order
    std::int32_t mutationSerial{};
    std::uint32_t flags{};
    std::array<std::byte, kTailPaddingSize> tailPadding{};
};

#pragma pack(pop)

static_assert(sizeof(Entry) == kEntrySize);
static_assert(std::is_standard_layout_v<Entry>);
static_assert(std::is_trivially_copyable_v<Entry>);
static_assert(offsetof(Entry, definitionIndex) == kDefinitionIndexOffset);
static_assert(offsetof(Entry, instanceSoid) == kInstanceSoidOffset);
static_assert(offsetof(Entry, quantity) == kQuantityOffset);
static_assert(offsetof(Entry, mutationSerial) == kMutationSerialOffset);
static_assert(offsetof(Entry, flags) == kFlagsOffset);

} // namespace dawn::middleware::datagen::family4::inventory::layout
