#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::global_activity_state {

inline constexpr std::uint32_t kMessageType = 1;
inline constexpr std::size_t kEncodedSize = 146;
inline constexpr std::size_t kMeaningfulBitCount = 1'161;

// wire bit 0 arms the initial slice set; with it clear the client never reads the
// pair below
inline constexpr std::size_t kInitialSliceSetArmBit = 0;
inline constexpr std::size_t kBubbleCountBit = 67;
inline constexpr std::uint8_t kBubbleCountWidth = 32;
// biased, so a zeroed body decodes to a large negative, not zero
inline constexpr std::uint32_t kBubbleCountBias = 0x80000000U;
inline constexpr std::size_t kBubbleStateBit = 99;
inline constexpr std::size_t kBubbleStateCount = 64;
inline constexpr std::uint8_t kBubbleStateNone = 0x7F;
inline constexpr std::size_t kSliceSetBit = 611;
inline constexpr std::uint8_t kSliceSetWidth = 10;
inline constexpr std::uint32_t kSliceSetBias = 1;
inline constexpr std::size_t kSpawnSetBit = 621;
inline constexpr std::uint8_t kSpawnSetWidth = 32;
inline constexpr std::size_t kDescriptorBit = 686;
inline constexpr std::uint8_t kDescriptorReasonWidth = 4;
inline constexpr std::uint8_t kDescriptorIndexWidth = 12;
inline constexpr std::size_t kNamePresenceBit = 733;
inline constexpr std::size_t kNameBit = 734;
inline constexpr std::size_t kNameCapacity = 40;
inline constexpr std::uint8_t kNameBias = 0x80;
inline constexpr std::size_t kTailBits = 103;

static_assert(kNameBit + kNameCapacity * 8 + 4 + kTailBits == kMeaningfulBitCount);

struct GlobalActivityState final {
    std::array<char, kNameCapacity> name{};
    std::size_t nameLength{};
    std::array<std::uint8_t, kBubbleStateCount> bubbleStates{};
    std::uint32_t bubbleCount{};
    std::uint32_t sliceSetIndex{};
    bool hasSliceSet{};
    std::uint32_t spawnSetHash{};
    std::int32_t reason{-1};
    std::int32_t fromActivityIndex{-1};
    std::int32_t activityIndex{-1};
    std::span<const std::byte> descriptorBits{};
    std::size_t descriptorBitLength{};
};

[[nodiscard]] bool encode_global_activity_state(const GlobalActivityState& state,
                                                std::span<std::byte> output,
                                                std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::activity_message::global_activity_state
