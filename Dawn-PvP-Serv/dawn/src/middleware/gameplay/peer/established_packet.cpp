#include "established_packet.h"

#include <array>

#include "../../encoding/bit_raw.h"
#include "peer_container.h"

namespace dawn::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

constexpr std::uint64_t kEstablishedMarker = 0;
constexpr std::uint8_t kFlagWidth = 1;
constexpr std::uint8_t kSequenceGuardWidth = 2;
constexpr std::uint8_t kOutboundHeadWidth = 10;
constexpr std::uint8_t kCursorWidth = 7;
constexpr std::uint8_t kAckBaseWidth = 7;
constexpr std::uint8_t kStatusPrefixWidth = 2;
constexpr std::uint8_t kStatusExtensionWidth = 1;
constexpr std::uint64_t kStatusEmpty = 0;
constexpr std::uint64_t kStatusSingle = 1;
constexpr std::uint64_t kStatusExplicit = 2;
constexpr std::uint64_t kStatusExtended = 3;
constexpr std::uint64_t kStatusLongSpan = 0;
constexpr std::uint8_t kStatusCountWidth = 7;
constexpr std::uint8_t kSingleIndexWidth = 3;
constexpr std::uint8_t kDelayWidth = 10;
constexpr std::uint64_t kMaximumStatusCount = kMaximumAckStatuses;
constexpr std::uint8_t kSelectorWidth = 2;
constexpr std::uint64_t kSelectorEnd = 0;
constexpr std::uint64_t kSelectorAbsolute = 1;
constexpr std::uint64_t kSelectorDelta = 2;
constexpr std::uint8_t kAbsoluteSequenceWidth = 13;
constexpr std::uint16_t kSequenceModulus = 8192;
constexpr std::uint8_t kDeltaWidth = 4;
constexpr std::uint8_t kLargeShortLengthWidth = 8;
constexpr std::uint8_t kSmallShortLengthWidth = 6;
constexpr std::uint16_t kByteBits = 8;
constexpr std::uint8_t kMessageIdWidth = 6;
constexpr std::uint8_t kMessageSizeWidth = 18;

[[nodiscard]] bool read_status(bits::Reader& reader, bool& received, std::uint8_t& status) noexcept {
    std::uint64_t first = 0;
    if (!reader.read(kFlagWidth, first)) {
        return false;
    }
    if (first == 0) {
        received = true;
        status = 1;
        return true;
    }
    std::uint64_t second = 0;
    if (!reader.read(kFlagWidth, second)) {
        return false;
    }
    // code 0 is status 1 (received), 10 is status 0 (unresolved), 11 is status 2 (out of order)
    received = second != 0;
    status = second != 0 ? 2 : 0;
    return true;
}

[[nodiscard]] bool write_status(bits::Writer& writer, bool received) noexcept {
    if (received) {
        return writer.write(0, kFlagWidth);
    }
    return writer.write(1, kFlagWidth) && writer.write(0, kFlagWidth);
}

// false entries after the run are packets from before this connection, not gaps; a true
// entry after the first false one is a real hole and needs the explicit form
[[nodiscard]] bool history_is_contiguous(
    std::span<const bool> received) noexcept {
    bool reachedUntrackedHistory = false;
    for (const bool entry : received) {
        if (!entry) {
            reachedUntrackedHistory = true;
        } else if (reachedUntrackedHistory) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_ack(bits::Reader& reader, AckState& output) noexcept {
    std::uint64_t present = 0;
    std::uint64_t head = 0;
    std::uint64_t cursor = 0;
    if (!reader.read(kFlagWidth, present) || !reader.read(kOutboundHeadWidth, head)
        || !reader.read(kCursorWidth, cursor)) {
        return false;
    }
    output.outboundHeadPresent = present != 0;
    output.outboundHead = static_cast<std::uint16_t>(head);
    output.headMinusCursor = static_cast<std::uint8_t>(cursor);

    std::uint64_t base = 0;
    std::uint64_t prefix = 0;
    if (!reader.read(kAckBaseWidth, base) || !reader.read(kStatusPrefixWidth, prefix)) {
        return false;
    }
    output.receiveHead = static_cast<std::uint16_t>(base);
    output.received = {};
    output.nativeStatuses = {};
    output.reportedCount = 0;
    output.idle = false;
    if (prefix == kStatusExtended) {
        std::uint64_t extension = 0;
        if (!reader.read(kStatusExtensionWidth, extension)) {
            return false;
        }
        if (extension != kStatusLongSpan) {
            output.ringInitialized = false;
            return true;
        }
        std::uint64_t count = 0;
        // the field is the number of statuses, not one less than it
        if (!reader.read(kStatusCountWidth, count) || count > kMaximumStatusCount) {
            return false;
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            bool received = false;
            std::uint8_t status{};
            if (!read_status(reader, received, status)) {
                return false;
            }
            if (index < output.received.size()) {
                output.received[index] = received;
                output.nativeStatuses[index] = status;
                output.reportedCount = static_cast<std::uint8_t>(index + 1);
            }
        }
    } else if (prefix == kStatusExplicit) {
        for (std::size_t index = 0; index < kAckHistory; ++index) {
            if (!read_status(reader, output.received[index], output.nativeStatuses[index])) {
                return false;
            }
        }
        output.reportedCount = static_cast<std::uint8_t>(kAckHistory);
    } else if (prefix == kStatusSingle) {
        std::uint64_t outOfOrder = 0;
        std::uint64_t index = 0;
        if (!reader.read(kFlagWidth, outOfOrder) || !reader.read(kSingleIndexWidth, index)) {
            return false;
        }
        output.received.fill(true);
        output.nativeStatuses.fill(1);
        output.received[static_cast<std::size_t>(index)] = outOfOrder != 0;
        output.nativeStatuses[static_cast<std::size_t>(index)] = outOfOrder != 0 ? 2 : 0;
        output.reportedCount = static_cast<std::uint8_t>(index + 1);
    } else {
        std::uint64_t ringState = 0;
        if (!reader.read(kFlagWidth, ringState)) {
            return false;
        }
        output.idle = ringState != 0;
    }
    std::uint64_t delay = 0;
    if (!reader.read(kDelayWidth, delay)) {
        return false;
    }
    output.delay = static_cast<std::uint16_t>(delay);
    output.ringInitialized = true;
    return true;
}

[[nodiscard]] bool read_queue(bits::Reader& reader,
                              std::size_t fragmentBytes,
                              std::uint8_t shortLengthWidth,
                              QueueRecords& output) noexcept {
    output.count = 0;
    std::uint64_t recordsPresent = 0;
    if (!reader.read(kFlagWidth, recordsPresent)) {
        return false;
    }
    if (recordsPresent == 0) {
        return true;
    }
    std::uint16_t previous = 0;
    bool hasPrevious = false;
    for (;;) {
        std::uint64_t selector = 0;
        if (!reader.read(kSelectorWidth, selector)) {
            return false;
        }
        if (selector == kSelectorEnd) {
            return true;
        }
        if (output.count >= output.records.size()) {
            return false;
        }
        QueueRecord& record = output.records[output.count];
        record = {};
        if (selector == kSelectorAbsolute) {
            std::uint64_t sequence = 0;
            if (!reader.read(kAbsoluteSequenceWidth, sequence)) {
                return false;
            }
            record.sequence = static_cast<std::uint16_t>(sequence);
        } else if (selector == kSelectorDelta) {
            std::uint64_t delta = 0;
            if (!hasPrevious || !reader.read(kDeltaWidth, delta)) {
                return false;
            }
            record.sequence = static_cast<std::uint16_t>((previous + 1 + delta) % kSequenceModulus);
        } else {
            if (!hasPrevious) {
                return false;
            }
            record.sequence = static_cast<std::uint16_t>((previous + 1) % kSequenceModulus);
        }
        previous = record.sequence;
        hasPrevious = true;

        std::uint64_t isShort = 0;
        if (!reader.read(kFlagWidth, isShort)) {
            return false;
        }
        record.shortFragment = isShort != 0;
        if (record.shortFragment) {
            std::uint64_t secondary = 0;
            std::uint64_t lengthMinusOne = 0;
            if (!reader.read(kFlagWidth, secondary)
                || !reader.read(shortLengthWidth, lengthMinusOne)) {
                return false;
            }
            record.bitCount = static_cast<std::uint16_t>(lengthMinusOne + 1);
        } else {
            record.bitCount = static_cast<std::uint16_t>(fragmentBytes * kByteBits);
        }
        if (record.bitCount > record.bytes.size() * kByteBits) {
            return false;
        }
        const std::size_t wholeBytes = record.bitCount / kByteBits;
        const std::uint8_t remainder = static_cast<std::uint8_t>(record.bitCount % kByteBits);
        if (!bits::read_raw(reader, std::span<std::byte>(record.bytes.data(), wholeBytes))) {
            return false;
        }
        if (remainder != 0) {
            std::uint64_t tail = 0;
            if (!reader.read(remainder, tail)) {
                return false;
            }
            record.bytes[wholeBytes] = static_cast<std::byte>(tail << (kByteBits - remainder));
        }
        ++output.count;
    }
}

} // namespace

bool decode_established(std::span<const std::byte> payload,
                        bool expectExternal,
                        EstablishedPacket& output) noexcept {
    bits::Reader reader(payload);
    std::uint64_t marker = 0;
    std::uint64_t fragmented = 0;
    std::uint64_t guard = 0;
    if (!reader.read(kFlagWidth, marker) || marker != kEstablishedMarker
        || !reader.read(kFlagWidth, fragmented) || fragmented != 0
        || !reader.read(kSequenceGuardWidth, guard)) {
        return false;
    }
    output = {};
    output.connectionSequenceLow2 = static_cast<std::uint8_t>(guard);
    if (!read_ack(reader, output.ack)
        || !read_queue(reader, kLargeFragmentBytes, kLargeShortLengthWidth, output.large)
        || !read_queue(reader, kSmallFragmentBytes, kSmallShortLengthWidth, output.small)) {
        return false;
    }
    // keep the post-queue prefix before its first bit: a present external handler has native
    // 16D4A00 sentinel 1 before common presence, an absent one goes straight to filler presence
    output.hasExternal = expectExternal;
    output.externalBitOffset = payload.size() * kByteBits - reader.remaining_bits();
    return true;
}

ExternalTailResult validate_absent_external_tail(std::span<const std::byte> payload,
                                                std::size_t bitOffset) noexcept {
    bits::Reader reader(payload);
    std::uint64_t present = 0;
    if (!reader.skip(bitOffset) || !reader.read(1, present)) return ExternalTailResult::malformed;
    if (present) return ExternalTailResult::present_unsupported;
    if (!reader.read(1, present)) return ExternalTailResult::malformed;
    if (present) {
        std::uint64_t count = 0;
        if (!reader.read(14, count) || !count || count > 9920 || !reader.skip(count))
            return ExternalTailResult::malformed;
    }
    std::uint64_t padding = 0;
    if (reader.remaining_bits() > 7
        || !reader.read(static_cast<std::uint8_t>(reader.remaining_bits()), padding) || padding)
        return ExternalTailResult::malformed;
    return ExternalTailResult::accepted_absent;
}

bool acknowledgement_covers(const AckState& ack, std::uint16_t sentSequence,
                            std::uint16_t resolvedBase) noexcept {
    if (!ack.ringInitialized || ack.reportedCount > ack.received.size()
        || sentSequence >= state::gameplay::kPacketSequenceModulus
        || resolvedBase >= state::gameplay::kPacketSequenceModulus
        || resolvedBase % kPacketRingSize != ack.receiveHead % kPacketRingSize) {
        return false;
    }
    // base is already resolved against retained history; never lift a repeated low-seven-bit
    // ACK to a newer outbound ring here
    constexpr auto modulus = state::gameplay::kPacketSequenceModulus;
    const auto distance = (resolvedBase + modulus - sentSequence) % modulus;
    if (distance == 0) {
        return true;
    }
    if (distance <= ack.reportedCount) {
        return ack.received[distance - 1U];
    }
    return false;
}

bool write_head_and_ack(bits::Writer& writer,
                        std::uint8_t connectionSequenceLow2,
                        const AckState& ack) noexcept {
    if (!writer.write(kEstablishedMarker, kFlagWidth) || !writer.write(0, kFlagWidth)
        || !writer.write(connectionSequenceLow2, kSequenceGuardWidth)) {
        return false;
    }
    if (!writer.write(ack.outboundHeadPresent ? 1U : 0U, kFlagWidth)
        || !writer.write(ack.outboundHead, kOutboundHeadWidth)
        || !writer.write(ack.headMinusCursor, kCursorWidth)) {
        return false;
    }
    if (!ack.ringInitialized) {
        return writer.write(0, kAckBaseWidth) && writer.write(kStatusExtended, kStatusPrefixWidth)
               && writer.write(1, kStatusExtensionWidth);
    }
    if (!writer.write(ack.receiveHead, kAckBaseWidth)) {
        return false;
    }
    if (ack.reportedCount) {
        if (ack.reportedCount > ack.received.size()
            || !writer.write(kStatusExtended, kStatusPrefixWidth)
            || !writer.write(kStatusLongSpan, kStatusExtensionWidth)
            || !writer.write(ack.reportedCount, kStatusCountWidth)) return false;
        for (std::size_t index = 0; index < ack.reportedCount; ++index)
            if (!write_status(writer, ack.received[index])) return false;
        return writer.write(ack.delay, kDelayWidth);
    }
    const auto history = std::span<const bool>(ack.received).first(kAckHistory);
    if (history_is_contiguous(history)) {
        // captured client packets use this form whenever history has no holes; the trailing bit
        // reports idle, it does not acknowledge older unnamed packets
        return writer.write(kStatusEmpty, kStatusPrefixWidth)
               && writer.write(1, kFlagWidth) && writer.write(ack.delay, kDelayWidth);
    }
    if (!writer.write(kStatusExplicit, kStatusPrefixWidth)) {
        return false;
    }
    for (const bool received : history) {
        if (!write_status(writer, received)) {
            return false;
        }
    }
    return writer.write(ack.delay, kDelayWidth);
}

bool write_empty_queue(bits::Writer& writer) noexcept {
    return writer.write(0, kFlagWidth);
}

bool write_queue(bits::Writer& writer,
                 const state::gameplay::OutboundQueue& queue,
                 std::size_t recordCount) noexcept {
    if (recordCount > queue.count) {
        return false;
    }
    if (recordCount == 0) {
        return write_empty_queue(writer);
    }
    if (!writer.write(1, kFlagWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < recordCount; ++index) {
        const state::gameplay::OutboundFragment& fragment = queue.fragments[index];
        if (!writer.write(kSelectorAbsolute, kSelectorWidth)
            || !writer.write(fragment.sequence, kAbsoluteSequenceWidth)
            || !writer.write(fragment.shortFragment ? 1U : 0U, kFlagWidth)) {
            return false;
        }
        if (fragment.shortFragment) {
            if (!writer.write(0, kFlagWidth)
                || !writer.write(fragment.bitCount - 1U, kLargeShortLengthWidth)) {
                return false;
            }
        }
        const std::size_t wholeBytes = fragment.bitCount / kByteBits;
        const auto remainder = static_cast<std::uint8_t>(fragment.bitCount % kByteBits);
        if (!bits::write_raw(writer, {fragment.bytes.data(), wholeBytes})) {
            return false;
        }
        if (remainder != 0) {
            const auto tail = std::to_integer<std::uint64_t>(fragment.bytes[wholeBytes]);
            if (!writer.write(tail >> (kByteBits - remainder), remainder)) {
                return false;
            }
        }
    }
    return writer.write(kSelectorEnd, kSelectorWidth);
}

bool enqueue_message(state::gameplay::OutboundQueue& queue,
                     std::uint8_t id,
                     std::uint32_t declaredSize,
                     std::span<const std::byte> body,
                     std::size_t bodyBits) noexcept {
    if (id > kMaximumMessageId) {
        return false;
    }
    std::array<std::byte, state::gameplay::kReassemblyCapacity> staged{};
    bits::Writer writer(staged);
    if (!writer.write(id, kMessageIdWidth) || !writer.write(declaredSize, kMessageSizeWidth)) {
        return false;
    }
    bits::Reader reader(body);
    std::size_t remaining = bodyBits;
    while (remaining != 0) {
        const auto width = static_cast<std::uint8_t>(remaining < kByteBits ? remaining : kByteBits);
        std::uint64_t value = 0;
        if (!reader.read(width, value) || !writer.write(value, width)) {
            return false;
        }
        remaining -= width;
    }
    std::size_t stagedBytes = 0;
    if (!writer.finish(stagedBytes)) {
        return false;
    }

    const std::size_t totalBits = writer.bit_count();
    constexpr std::size_t kFragmentBits = kLargeFragmentBytes * kByteBits;
    std::size_t consumed = 0;
    bits::Reader source({staged.data(), stagedBytes});
    // a half-enqueued message can never be reassembled, so any failure below restores the queue
    const std::size_t entryCount = queue.count;
    const std::uint16_t entrySequence = queue.nextSequence;
    const auto restore = [&queue, entryCount, entrySequence]() noexcept {
        for (std::size_t index = entryCount; index < queue.count; ++index) {
            queue.fragments[index] = {};
        }
        queue.count = entryCount;
        queue.nextSequence = entrySequence;
    };
    while (consumed < totalBits) {
        if (queue.count >= queue.fragments.size()) {
            restore();
            return false;
        }
        const std::size_t take =
            (totalBits - consumed) < kFragmentBits ? totalBits - consumed : kFragmentBits;
        const bool last = consumed + take >= totalBits;
        state::gameplay::OutboundFragment& fragment = queue.fragments[queue.count];
        fragment = {};
        bits::Writer chunk(fragment.bytes);
        std::size_t written = 0;
        std::size_t pending = take;
        while (pending != 0) {
            const auto width = static_cast<std::uint8_t>(pending < kByteBits ? pending : kByteBits);
            std::uint64_t value = 0;
            if (!source.read(width, value) || !chunk.write(value, width)) {
                restore();
                return false;
            }
            pending -= width;
        }
        if (!chunk.finish(written)) {
            restore();
            return false;
        }
        fragment.sequence = queue.nextSequence;
        fragment.bitCount = static_cast<std::uint16_t>(take);
        // PC 1.59 closes the message on the final fragment even when it uses all 256 bits; the
        // short-record length stores length-minus-one so it can represent 256
        fragment.shortFragment = last;
        fragment.occupied = true;
        queue.nextSequence = static_cast<std::uint16_t>((queue.nextSequence + 1)
                                                        % state::gameplay::kMessageSequenceModulus);
        ++queue.count;
        consumed += take;
    }
    return true;
}

bool write_absent_filler(bits::Writer& writer) noexcept {
    return writer.write(0, kFlagWidth) && writer.write(0, kFlagWidth);
}

} // namespace dawn::middleware::gameplay::peer
