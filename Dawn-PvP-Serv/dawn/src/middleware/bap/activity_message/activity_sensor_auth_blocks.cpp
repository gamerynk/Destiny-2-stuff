#include <algorithm>
#include "sensor_auth_update.h"

namespace dawn::middleware::bap::activity_message::sensor_auth_update {
namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kChunkWidth = 32;

} // namespace

bool pad_bits(bits::Writer& writer, std::size_t count) noexcept {
    bool encoded = true;
    for (std::size_t written = 0; encoded && written < count; written += kChunkWidth) {
        const std::size_t remaining = count - written;
        const auto width =
            static_cast<std::uint8_t>(remaining > kChunkWidth ? kChunkWidth : remaining);
        encoded = writer.write(0, width);
    }
    return encoded;
}

bool write_bubble_block(bits::Writer& writer, const Grant& grant) noexcept {
    bool encoded = true;
    for (std::size_t bubble = 0; encoded && bubble < kAuthoritySlotCount; ++bubble) {
        encoded = writer.write(bubble == grant.bubble ? 1U : 0U, kPresenceWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth) && writer.write(0, kPresenceWidth);
    for (std::size_t bubble = 0; encoded && bubble < kAuthoritySlotCount; ++bubble) {
        const bool granted = bubble == grant.bubble;
        // host token stays absent; a wire copy that differs from the mirror parks a 5 s stamp
        encoded =
            writer.write(0, kPresenceWidth) && writer.write(granted ? 1U : 0U, kPresenceWidth);
        if (encoded && granted) {
            encoded = writer.write(grant.token, kGrantTokenWidth);
        }
        encoded = encoded && writer.write(0, kPresenceWidth);
    }
    return encoded;
}

namespace {

[[nodiscard]] bool write_key_mask(bits::Writer& writer, std::size_t keyCount) noexcept {
    bool encoded = true;
    for (std::size_t word = 0; encoded && word < kBubbleMaskWords; ++word) {
        const std::size_t low = word * kChunkWidth;
        const std::size_t set = keyCount > low ? keyCount - low : 0;
        const std::size_t width = set > kChunkWidth ? kChunkWidth : set;
        encoded = writer.write((std::uint64_t{1} << width) - 1, kChunkWidth);
    }
    return encoded;
}

[[nodiscard]] bool write_bubble_sub_block(bits::Writer& writer,
                                          const BubbleSubBlock& block,
                                          std::uint8_t stateSequence) noexcept {
    const std::size_t keyCount = block.keys.size();
    const auto count = static_cast<std::uint32_t>(keyCount);
    bool encoded = writer.write(1, kPresenceWidth)
                   && writer.write(kBubbleKeyBias + block.bubble, kKeyWidth)
                   && writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
                   && writer.write(count, kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < keyCount; ++index) {
        encoded = writer.write(block.keys[index], kKeyWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth) && write_key_mask(writer, keyCount)
              && writer.write(1, kPresenceWidth) && writer.write(count, kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < keyCount; ++index) {
        encoded = writer.write(kStateByteBias + stateSequence, 8);
    }
    return encoded;
}

[[nodiscard]] bool write_bubble_sub_blocks(bits::Writer& writer,
                                           std::span<const BubbleSubBlock> subBlocks,
                                           std::uint8_t stateSequence) noexcept {
    bool encoded = writer.write(static_cast<std::uint32_t>(subBlocks.size()), kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < subBlocks.size(); ++index) {
        encoded = write_bubble_sub_block(writer, subBlocks[index], stateSequence);
    }
    return encoded;
}

} // namespace

bool write_roster_delta(bits::Writer& writer,
                        const Roster& roster,
                        std::uint8_t stateSequence) noexcept {
    const std::size_t root = writer.bit_count();
    const std::size_t keyCount = roster.topLevelGroupCount;
    if (keyCount > kTopLevelKeyCapacity || keyCount > roster.groupCount
        || roster.groupCount > roster.groups.size()) return false;
    if (roster.bubbleSubBlocks.size() > kBubbleSubBlockCapacity) return false;
    for (const auto& block : roster.bubbleSubBlocks)
        if (block.bubble > kMaximumSubBlockBubble || block.keys.empty()
            || block.keys.size() > kBubbleKeyCapacity) return false;
    bool encoded = writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
                   && writer.write(1, kPresenceWidth)
                   && writer.write(static_cast<std::uint32_t>(keyCount), kDeltaCountWidth)
                   && writer.bit_count() == root + kDeltaKeysBit;
    for (std::size_t group = 0; encoded && group < keyCount; ++group) {
        encoded = writer.write(roster.groups[group].key, kKeyWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth)
              && writer.bit_count() == root + delta_mask_bit(keyCount);
    // a key whose mask bit is clear is dropped silently, so the mask must match the key count
    for (std::size_t word = 0; encoded && word < kDeltaMaskWords; ++word) {
        const auto first = word * kChunkWidth;
        const auto width = keyCount > first ? (std::min)(keyCount-first, std::size_t{kChunkWidth}) : 0;
        encoded = writer.write((std::uint64_t{1} << width)-1, kChunkWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth)
              && writer.bit_count() == root + delta_state_count_bit(keyCount)
              && writer.write(static_cast<std::uint32_t>(keyCount), kDeltaCountWidth);
    for (std::size_t group = 0; encoded && group < keyCount; ++group) {
        encoded = writer.write(kStateByteBias + stateSequence, 8);
    }
    const std::span<const BubbleSubBlock> subBlocks = roster.bubbleSubBlocks;
    encoded = encoded && writer.write(subBlocks.empty() ? 0U : 1U, kPresenceWidth);
    if (encoded && !subBlocks.empty()) {
        encoded = write_bubble_sub_blocks(writer, subBlocks, stateSequence);
    }
    return encoded && writer.bit_count() == root + delta_bits(keyCount, subBlocks);
}

bool write_object_block(bits::Writer& writer,
                        const Snapshot& snapshot,
                        std::uint32_t key,
                        std::uint8_t slotType,
                        std::uint16_t slotIndex,
                        std::uint8_t flags,
                        bool carriesPlayerKey) noexcept {
    const bool emitAuth = (flags & kSlotAuthFlag) != 0;
    const bool emitSense = (flags & kSlotSenseFlag) != 0;
    const std::size_t body =
        emitAuth ? auth_body_bits(snapshot, key, slotType, slotIndex, carriesPlayerKey) : 0;
    const std::size_t remainder = (emitAuth ? 2U : 0U) + (emitSense ? 1U : 0U) + body;
    bool encoded = writer.write(1, kPresenceWidth) && writer.write(key, kKeyWidth)
                   && writer.write(std::uint32_t{slotType} + kSlotTypeBias, kSlotTypeWidth)
                   && writer.write(std::uint32_t{slotIndex} + kSlotIndexBias, kSlotIndexWidth)
                   && writer.write(static_cast<std::uint32_t>(remainder), kKeyWidth);
    const std::size_t start = writer.bit_count();
    if (encoded && emitAuth) {
        // a reset bit of zero on a first block decodes into a throwaway buffer and never seeds
        encoded =
            writer.write(1, kPresenceWidth) && writer.write(body > 0 ? 1U : 0U, kPresenceWidth);
        if (encoded && body > 0) {
            encoded =
                write_auth_body(writer, snapshot, key, slotType, slotIndex, carriesPlayerKey);
        }
    }
    // a sense-present bit of one costs 35 more bits, so always send it absent
    if (encoded && emitSense) {
        encoded = writer.write(0, kPresenceWidth);
    }
    return encoded && writer.bit_count() == start + remainder;
}

} // namespace dawn::middleware::bap::activity_message::sensor_auth_update
