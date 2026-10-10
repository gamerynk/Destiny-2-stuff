#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::client::content::handles::layout {

inline constexpr std::size_t kDescriptorMiddleByteCount = 32;
inline constexpr std::size_t kDescriptorTailByteCount = 8;
inline constexpr std::size_t kTableDescriptorSize = 64;
inline constexpr std::size_t kRecordPrefixSize = 16;

struct TableDescriptor {
    std::uint64_t opaque00{};
    std::uintptr_t recordArray{};
    std::array<std::byte, kDescriptorMiddleByteCount> opaque16{};
    std::uint32_t recordStride{};
    std::int32_t correctionMask{};
    std::array<std::byte, kDescriptorTailByteCount> opaque56{};
};

struct RecordPrefix {
    std::uint64_t opaque00{};
    std::uint64_t correctionSource{};
};

static_assert(sizeof(std::uintptr_t) == sizeof(std::uint64_t));
static_assert(offsetof(TableDescriptor, recordArray) == sizeof(std::uint64_t));
static_assert(offsetof(TableDescriptor, recordStride)
              == sizeof(std::uint64_t) + sizeof(std::uintptr_t) + kDescriptorMiddleByteCount);
static_assert(offsetof(TableDescriptor, correctionMask)
              == offsetof(TableDescriptor, recordStride) + sizeof(std::uint32_t));
static_assert(offsetof(RecordPrefix, correctionSource) == sizeof(std::uint64_t));
static_assert(sizeof(TableDescriptor) == kTableDescriptorSize);
static_assert(sizeof(RecordPrefix) == kRecordPrefixSize);
static_assert(std::is_trivially_copyable_v<TableDescriptor>);
static_assert(std::is_trivially_copyable_v<RecordPrefix>);

} // namespace dawn::client::content::handles::layout
