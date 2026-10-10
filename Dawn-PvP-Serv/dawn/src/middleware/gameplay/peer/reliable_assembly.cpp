#include "reliable_assembly.h"

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "peer_container.h"

namespace dawn::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kIdWidth = 6;
constexpr std::uint8_t kSizeWidth = 18;
constexpr std::size_t kByteBits = 8;

[[nodiscard]] std::uint16_t distance(std::uint16_t from, std::uint16_t to) noexcept {
    // add the modulus before subtracting; a bare difference is signed and goes negative on a wrap
    return static_cast<std::uint16_t>((to + state::gameplay::kMessageSequenceModulus - from)
                                      % state::gameplay::kMessageSequenceModulus);
}

[[nodiscard]] std::size_t slot_of(std::uint16_t sequence) noexcept {
    return sequence % state::gameplay::kReliableSlots;
}

} // namespace

std::size_t accept_records(const QueueRecords& records,
                           state::gameplay::ReliableQueue& queue) noexcept {
    std::size_t dropped = 0;
    for (std::size_t index = 0; index < records.count; ++index) {
        const QueueRecord& record = records.records[index];
        if (!queue.started) {
            queue.started = true;
            queue.nextSequence = record.sequence;
        }
        if (distance(queue.nextSequence, record.sequence) >= state::gameplay::kReliableSlots) {
            ++dropped;
            continue;
        }
        state::gameplay::ReliableFragment& fragment = queue.fragments[slot_of(record.sequence)];
        if (fragment.occupied && fragment.sequence == record.sequence) {
            continue;
        }
        fragment.sequence = record.sequence;
        fragment.bitCount = record.bitCount;
        fragment.shortFragment = record.shortFragment;
        fragment.bytes = record.bytes;
        fragment.occupied = true;
    }
    return dropped;
}

bool drain_message(state::gameplay::ReliableQueue& queue, AssembledMessage& output) noexcept {
    if (!queue.started) {
        return false;
    }
    std::size_t runLength = 0;
    std::size_t totalBits = 0;
    for (; runLength < state::gameplay::kReliableSlots; ++runLength) {
        const auto sequence = static_cast<std::uint16_t>(
            (queue.nextSequence + runLength) % state::gameplay::kMessageSequenceModulus);
        const state::gameplay::ReliableFragment& fragment = queue.fragments[slot_of(sequence)];
        if (!fragment.occupied || fragment.sequence != sequence) {
            return false;
        }
        totalBits += fragment.bitCount;
        if (fragment.shortFragment) {
            break;
        }
    }
    if (runLength >= state::gameplay::kReliableSlots
        || totalBits < kIdWidth + kSizeWidth
        || totalBits > state::gameplay::kReassemblyCapacity * kByteBits) {
        return false;
    }

    AssembledMessage candidate{};
    bits::Writer writer(candidate.bytes);
    for (std::size_t index = 0; index <= runLength; ++index) {
        const auto sequence = static_cast<std::uint16_t>(
            (queue.nextSequence + index) % state::gameplay::kMessageSequenceModulus);
        state::gameplay::ReliableFragment& fragment = queue.fragments[slot_of(sequence)];
        bits::Reader reader({fragment.bytes.data(), fragment.bytes.size()});
        std::size_t remaining = fragment.bitCount;
        while (remaining != 0) {
            const auto width =
                static_cast<std::uint8_t>(remaining < kByteBits ? remaining : kByteBits);
            std::uint64_t value = 0;
            if (!reader.read(width, value) || !writer.write(value, width)) {
                return false;
            }
            remaining -= width;
        }
        fragment = {};
    }
    queue.nextSequence = static_cast<std::uint16_t>((queue.nextSequence + runLength + 1)
                                                    % state::gameplay::kMessageSequenceModulus);

    std::size_t written = 0;
    if (!writer.finish(written)) {
        return false;
    }
    candidate.bitCount = totalBits;
    bits::Reader header({candidate.bytes.data(), written}, totalBits);
    std::uint64_t id = 0;
    std::uint64_t declared = 0;
    if (!header.read(kIdWidth, id) || id > kMaximumMessageId
        || !header.read(kSizeWidth, declared)) {
        return false;
    }
    candidate.id = static_cast<std::uint8_t>(id);
    candidate.declaredSize = static_cast<std::uint32_t>(declared);
    candidate.bodyBitOffset = kIdWidth + kSizeWidth;
    output = candidate;
    return true;
}

} // namespace dawn::middleware::gameplay::peer
