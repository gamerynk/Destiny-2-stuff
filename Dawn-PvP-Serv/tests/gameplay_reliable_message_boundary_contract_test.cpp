#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "middleware/encoding/bit_reader.h"
#include "middleware/gameplay/peer/established_packet.h"
#include "middleware/gameplay/peer/reliable_assembly.h"
#include "state/gameplay/definition.h"

namespace bits = dawn::middleware::encoding::bits;
namespace peer = dawn::middleware::gameplay::peer;
namespace state = dawn::state::gameplay;

namespace {

[[nodiscard]] bool round_trip(std::uint8_t id,
                              std::uint32_t declaredSize,
                              std::size_t bodyBits,
                              std::size_t expectedFragments,
                              std::size_t expectedTotalBits) noexcept {
    std::array<std::byte, state::kReassemblyCapacity> body{};
    state::OutboundQueue outbound{};
    if (!peer::enqueue_message(outbound, id, declaredSize, body, bodyBits)
        || outbound.count != expectedFragments) {
        return false;
    }
    std::size_t accumulated = 0;
    peer::QueueRecords records{};
    records.count = outbound.count;
    for (std::size_t index = 0; index < outbound.count; ++index) {
        const state::OutboundFragment& source = outbound.fragments[index];
        peer::QueueRecord& target = records.records[index];
        target.sequence = source.sequence;
        target.shortFragment = source.shortFragment;
        target.bitCount = source.bitCount;
        target.bytes = source.bytes;
        accumulated += source.bitCount;
        if (source.shortFragment != (index + 1 == outbound.count)) {
            return false;
        }
    }
    if (accumulated != expectedTotalBits
        || !outbound.fragments[outbound.count - 1].shortFragment) {
        return false;
    }

    state::ReliableQueue inbound{};
    if (peer::accept_records(records, inbound) != 0) {
        return false;
    }
    peer::AssembledMessage assembled{};
    if (!peer::drain_message(inbound, assembled) || assembled.id != id
        || assembled.declaredSize != declaredSize || assembled.bodyBitOffset != 24
        || assembled.bitCount != expectedTotalBits) {
        return false;
    }
    const std::size_t bytes = (assembled.bitCount + 7) / 8;
    bits::Reader exact({assembled.bytes.data(), bytes}, assembled.bitCount);
    std::uint64_t decodedId = 0;
    std::uint64_t decodedSize = 0;
    return exact.read(6, decodedId) && decodedId == id && exact.read(18, decodedSize)
           && decodedSize == declaredSize && exact.remaining_bits() == bodyBits;
}

} // namespace

int main() {
    if (!round_trip(38, 44064, 151, 1, 175)) {
        std::fputs("two-member ID38 reliable boundary failed\n", stderr);
        return 1;
    }
    if (!round_trip(39, 44056, 146, 1, 170)) {
        std::fputs("live ID39 reliable boundary failed\n", stderr);
        return 2;
    }
    // native PC1.59 closes an exact 256-bit message on that full fragment; no extra bit exists
    if (!round_trip(38, 44064, 232, 1, 256)) {
        std::fputs("exact-full-fragment close failed\n", stderr);
        return 3;
    }
    std::array<std::byte, 64> exactBody{};
    state::OutboundQueue exactQueue{};
    std::array<std::byte, 2048> packetStorage{};
    bits::Writer packetWriter(packetStorage);
    peer::AckState ack{};
    std::size_t packetBytes = 0;
    if (!peer::enqueue_message(exactQueue, 38, 44064, exactBody, 232)
        || !peer::write_head_and_ack(packetWriter, 0, ack)
        || !peer::write_queue(packetWriter, exactQueue, exactQueue.count)
        || !peer::write_empty_queue(packetWriter) || !peer::write_absent_filler(packetWriter)
        || !packetWriter.finish(packetBytes)) {
        std::fputs("exact-full-fragment packet fixture failed to encode\n", stderr);
        return 4;
    }
    peer::EstablishedPacket decodedPacket{};
    if (!peer::decode_established(
            std::span<const std::byte>(packetStorage.data(), packetBytes), false, decodedPacket)
        || decodedPacket.large.count != 1 || !decodedPacket.large.records[0].shortFragment
        || decodedPacket.large.records[0].bitCount != 256) {
        std::fputs("queue wire codec did not preserve short length 256\n", stderr);
        return 5;
    }
    if (!round_trip(38, 44064, 488, 2, 512)) {
        std::fputs("two-full-fragment close failed\n", stderr);
        return 6;
    }
    // transport ceiling is 8168 body bits: with 24 header bits that is 32 fragments
    if (!round_trip(38, 44064, 8168, state::kReliableSlots, 8192)) {
        std::fputs("maximum reliable body boundary failed\n", stderr);
        return 7;
    }
    state::OutboundQueue oversized{};
    oversized.nextSequence = 321;
    std::array<std::byte, state::kReassemblyCapacity + 1> oversizedBody{};
    if (peer::enqueue_message(oversized, 38, 44064, oversizedBody, 8169)
        || oversized.count != 0 || oversized.nextSequence != 321) {
        std::fputs("oversized reliable body was not rejected atomically\n", stderr);
        return 8;
    }

    state::OutboundQueue full{};
    full.count = state::kOutboundSlots;
    full.nextSequence = 123;
    full.fragments[0].occupied = true;
    full.fragments[0].sequence = 77;
    std::array<std::byte, 32> body{};
    if (peer::enqueue_message(full, 39, 44056, body, 146) || full.count != state::kOutboundSlots
        || full.nextSequence != 123 || !full.fragments[0].occupied
        || full.fragments[0].sequence != 77) {
        std::fputs("outbound queue failure did not roll back atomically\n", stderr);
        return 9;
    }

    peer::QueueRecords tooShort{};
    tooShort.count = 1;
    tooShort.records[0].sequence = state::kFirstMessageSequence;
    tooShort.records[0].shortFragment = true;
    tooShort.records[0].bitCount = 23;
    state::ReliableQueue inbound{};
    peer::AssembledMessage rejected{};
    if (peer::accept_records(tooShort, inbound) != 0 || peer::drain_message(inbound, rejected)) {
        std::fputs("sub-header reliable message was accepted from byte padding\n", stderr);
        return 10;
    }

    std::puts("PASS: reliable message bit boundaries and exact-full-fragment close are native");
    return 0;
}
