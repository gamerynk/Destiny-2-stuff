#include <array>
#include <cstdio>
#include <string_view>
#include "fixtures/native_ack_status_vectors.h"
#include "middleware/gameplay/peer/reliable_carriers.h"
#include "middleware/gameplay/peer/receive_history.h"
#include "middleware/gameplay/peer/inbound_ack_context.h"
#include "middleware/gameplay/peer/reliable_send_batch.h"

namespace peer = dawn::middleware::gameplay::peer;
namespace state = dawn::state::gameplay;
namespace bits = dawn::middleware::encoding::bits;
namespace {
unsigned checks = 0;
bool check(bool condition, std::string_view name) {
    ++checks;
    if (!condition) std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(name.size()), name.data());
    return condition;
}
unsigned nibble(char c) { return c <= '9' ? c - '0' : c - 'a' + 10U; }
bool native_vector(const native_ack_fixture::Vector& v) {
    std::array<std::byte, 128> storage{};
    bits::Writer writer(storage);
    if (!writer.write(0, 1) || !writer.write(0, 1) || !writer.write(2, 2)
        || !writer.write(1, 1) || !writer.write(1000, 10) || !writer.write(1, 7)
        || !writer.write(70, 7)) return false;
    for (std::size_t bit = 0; bit < v.bits; ++bit)
        if (!writer.write((nibble(v.wire[bit / 4]) >> (3 - bit % 4)) & 1U, 1)) return false;
    std::size_t size = 0;
    if (!writer.write(0, 1) || !writer.write(0, 1) || !writer.write(1, 1)
        || !writer.finish(size)) return false;
    peer::EstablishedPacket packet{};
    if (!peer::decode_established({storage.data(), size}, true, packet)
        || packet.connectionSequenceLow2 != 2 || packet.ack.outboundHead != 1000
        || !packet.ack.outboundHeadPresent || packet.ack.headMinusCursor != 1
        || packet.ack.receiveHead != 70 || packet.ack.reportedCount != v.statuses.size()
        || packet.ack.ringInitialized != v.initialized || packet.ack.idle != v.idle
        || packet.ack.delay != v.delay || packet.externalBitOffset != 31U + v.bits
        || packet.large.count != 0 || packet.small.count != 0) return false;
    for (std::size_t index = 0; index < v.statuses.size(); ++index)
        if (packet.ack.received[index] != (v.statuses[index] != '0')
            || packet.ack.nativeStatuses[index] != static_cast<std::uint8_t>(v.statuses[index]-'0')) return false;
    bits::Reader remaining({storage.data(), size});
    std::uint64_t sentinel = 0;
    return remaining.skip(packet.externalBitOffset) && remaining.read(1, sentinel) && sentinel == 1;
}
}

int main() {
    {
        state::OutboundQueue batch{};
        batch.count = 75;
        for (std::size_t i = 0; i < batch.count; ++i) {
            batch.fragments[i].sequence = static_cast<std::uint16_t>(i);
            batch.fragments[i].bitCount = 256;
            batch.fragments[i].shortFragment = true;
            batch.fragments[i].bytes.fill(static_cast<std::byte>(i));
        }
        if (!check(peer::reliable_send_batch(batch, false) == 16, "join backlog sends bounded batch")
            || !check(peer::reliable_send_batch(batch, true) == 1, "entity stream keeps reserved budget")) return 1;
        peer::AckState fullest{};
        fullest.ringInitialized = fullest.outboundHeadPresent = true;
        fullest.headMinusCursor = 127;
        fullest.reportedCount = 127;
        for (std::size_t i = 0; i < fullest.reportedCount; ++i) fullest.received[i] = (i % 2) != 0;
        std::array<std::byte, 1024> storage{};
        bits::Writer writer(storage);
        std::size_t written{};
        if (!check(peer::write_head_and_ack(writer, 0, fullest)
            && peer::write_queue(writer, batch, peer::reliable_send_batch(batch, false))
            && peer::write_empty_queue(writer) && peer::write_absent_filler(writer)
            && writer.finish(written), "full ACK history and batch fit production packet")) return 1;
        peer::EstablishedPacket decoded{};
        if (!check(peer::decode_established(std::span(storage).first(written), true, decoded)
            && decoded.large.count == 16, "native queue codec accepts all batched fragments")) return 1;
        for (std::size_t i = 0; i < decoded.large.count; ++i)
            if (!check(decoded.large.records[i].sequence == i && decoded.large.records[i].bitCount == 256
                && decoded.large.records[i].bytes == batch.fragments[i].bytes, "batch preserves fragment order and bytes")) return 1;
        batch.awaitingAcknowledgement = true;
        batch.sentCount = 16;
        batch.count = 90;
        if (!check(peer::reliable_send_batch(batch, false) == 16, "new messages cannot change retry prefix")) return 1;
        peer::record_carrier(batch, 20);
        peer::AckState receipt{}; receipt.ringInitialized = true; receipt.receiveHead = 19;
        if (!check(!peer::prefix_confirmed(batch, receipt, 20, 19), "lost batch remains pending")) return 1;
        receipt.receiveHead = 20;
        if (!check(peer::prefix_confirmed(batch, receipt, 20, 20), "positive carrier receipt settles batch")) return 1;
        batch = {}; batch.count = 3;
        if (!check(peer::reliable_send_batch(batch, false) == 3, "short queue has no fabricated records")) return 1;
        batch = {};
        if (!check(peer::reliable_send_batch(batch, false) == 0, "empty queue stays empty")) return 1;
    }
    for (const auto& v : native_ack_fixture::vectors)
        if (!check(native_vector(v), v.name)) return 1;

    state::PeerLink history{};
    peer::record_accepted_packet(history, 1020);
    peer::record_accepted_packet(history, 1023);
    peer::record_accepted_packet(history, 1);
    if (!check(history.receiveHead == 1 && history.receivedCount == 5
               && !history.received[0] && history.received[1] && !history.received[2]
               && !history.received[3] && history.received[4], "accepted head retains refused gaps")) return 1;
    peer::record_accepted_packet(history, 1022);
    if (!check(history.receiveHead == 1 && history.received[2] && !history.received[3],
               "late accepted packet closes only its own gap")) return 1;
    peer::record_accepted_packet(history, 1019);
    if (!check(history.receivedCount == 5, "preconnection packet cannot extend history")) return 1;
    peer::record_accepted_packet(history, 200);
    if (!check(history.receivedCount == 127 && !history.received[126], "large advance preserves bounded unresolved window")) return 1;
    peer::record_accepted_packet(history, 73);
    if (!check(history.received[126] && !history.received[125], "oldest named late receipt")) return 1;
    for (unsigned count = 0; count <= 127; ++count) {
        peer::AckState sent{};
        sent.ringInitialized = true;
        sent.outboundHeadPresent = true;
        sent.outboundHead = 999;
        sent.receiveHead = 200;
        sent.reportedCount = static_cast<std::uint8_t>(count);
        sent.delay = 1023;
        for (std::size_t i = 0; i < count; ++i) sent.received[i] = i % 4 == 0;
        std::array<std::byte, 128> storage{};
        bits::Writer writer(storage);
        std::size_t size = 0;
        peer::EstablishedPacket received{};
        if (!check(peer::write_head_and_ack(writer, 2, sent) && peer::write_empty_queue(writer)
                   && peer::write_empty_queue(writer) && peer::write_absent_filler(writer)
                   && writer.finish(size) && peer::decode_established({storage.data(), size}, false, received)
                   && received.ack.reportedCount == count && received.ack.receiveHead == 72
                   && received.ack.delay == 1023, "outbound long-span sizes")) return 1;
        for (std::size_t i = 0; i < count; ++i)
            if (!check(received.ack.received[i] == sent.received[i], "outbound long-span keeps each gap")) return 1;
        if (!check(peer::validate_absent_external_tail({storage.data(), size}, received.externalBitOffset)
                   == peer::ExternalTailResult::accepted_absent, "absent external tail accepts exact padding")) return 1;
        storage[size - 1] |= std::byte{1};
        if (!check(peer::validate_absent_external_tail({storage.data(), size}, received.externalBitOffset)
                   != peer::ExternalTailResult::accepted_absent, "nonzero tail cannot retire native state")) return 1;
    }
    for (unsigned filler : {0U, 1U, 7U, 8U, 9920U, 9921U, 16383U}) {
        std::array<std::byte, 2050> storage{};
        bits::Writer writer(storage);
        std::size_t size = 0;
        if (!writer.write(0, 1) || !writer.write(1, 1) || !writer.write(filler, 14)) return 1;
        for (unsigned bit = 0; bit < filler; ++bit) if (!writer.write(bit & 1U, 1)) return 1;
        if (!writer.finish(size)) return 1;
        if (!check((peer::validate_absent_external_tail({storage.data(), size}, 0)
                    == peer::ExternalTailResult::accepted_absent) == (filler >= 1 && filler <= 9920),
                   "native filler bounds")) return 1;
        if (filler >= 8 && !check(peer::validate_absent_external_tail({storage.data(), size - 1}, 0)
                                   == peer::ExternalTailResult::malformed, "truncated filler refuses")) return 1;
    }
    const std::array<std::byte, 1> present{std::byte{0x80}};
    if (!check(peer::validate_absent_external_tail(present, 0) == peer::ExternalTailResult::present_unsupported,
               "unknown external stream stays unacknowledged")) return 1;

    peer::AckState ack{};
    ack.receiveHead = 20;
    if (!check(!peer::acknowledgement_covers(ack, 20, 20), "uninitialized ring")) return 1;
    ack.ringInitialized = true;
    ack.idle = true;
    if (!check(peer::acknowledgement_covers(ack, 20, 20), "base confirms itself")
        || !check(!peer::acknowledgement_covers(ack, 19, 20), "idle does not confirm older carrier")) return 1;
    ack.reportedCount = 127;
    ack.received[126] = true;
    ack.receiveHead = 10;
    if (!check(peer::acknowledgement_covers(ack, 907, 10), "long entry127 survives ten-bit wrap")
        || !check(!peer::acknowledgement_covers(ack, 9, 10), "named unresolved gap remains pending")
        || !check(!peer::acknowledgement_covers(ack, 906, 10), "outside named span remains pending")) return 1;
    ack.receiveHead = 11;
    ack.received.fill(true);
    if (!check(!peer::acknowledgement_covers(ack, 10, 10), "unresolved base mismatch refused")) return 1;

    for (unsigned latest = 0; latest < 1024; ++latest) {
        ack.receiveHead = static_cast<std::uint16_t>(latest % 128);
        ack.reportedCount = 63;
        for (std::size_t i = 0; i < ack.received.size(); ++i) ack.received[i] = i % 3 == 1;
        for (unsigned age = 0; age <= 64; ++age) {
            const auto carrier = static_cast<std::uint16_t>((latest + 1024 - age) % 1024);
            const bool expected = age == 0 || (age <= 63 && (age - 1) % 3 == 1);
            if (!check(peer::acknowledgement_covers(ack, carrier, static_cast<std::uint16_t>(latest)) == expected,
                       "ring wrap and explicit loss")) return 1;
        }
    }

    state::OutboundQueue queue{};
    queue.count = 2;
    queue.sentCount = 1;
    queue.awaitingAcknowledgement = true;
    peer::record_carrier(queue, 1022);
    peer::record_carrier(queue, 1023);
    peer::record_carrier(queue, 0);
    ack = {};
    ack.ringInitialized = true;
    ack.receiveHead = 0;
    if (!check(peer::prefix_confirmed(queue, ack, 0, 0), "retransmission confirms unchanged prefix")) return 1;
    ack.receiveHead = 1;
    ack.reportedCount = 3;
    if (!check(!peer::prefix_confirmed(queue, ack, 1, 1), "all carriers lost")) return 1;
    ack.received[2] = true;
    if (!check(peer::prefix_confirmed(queue, ack, 1, 1), "delayed first carrier receipt")) return 1;
    peer::record_carrier(queue, 0);
    if (!check(queue.carryingCount == 3, "duplicate carrier record")) return 1;
    for (std::uint16_t seq = 1; seq <= 70; ++seq) peer::record_carrier(queue, seq);
    if (!check(queue.carryingCount == 32 && queue.carryingPackets[0] == 39
               && queue.carryingPackets[31] == 70, "carrier capacity bound")) return 1;
    ack.receiveHead = 70;
    if (!check(peer::prefix_confirmed(queue, ack, 70, 70), "retry after original carrier expires")) return 1;
    ack.receiveHead = 1022 % 128;
    ack.reportedCount = 0;
    if (!check(!peer::prefix_confirmed(queue, ack, 70, 1022), "old ring receipt does not confirm new prefix")) return 1;
    peer::record_carrier(queue, 140);
    if (!check(queue.carryingCount == 1 && queue.carryingPackets[0] == 140, "age64 expires without delivery")) return 1;
    ack.receiveHead = 140 % 128;
    if (!check(!peer::prefix_confirmed(queue, ack, 204, 140), "expired last carrier cannot settle")) return 1;
    queue = {};
    if (!check(!peer::prefix_confirmed(queue, ack, 140, 140), "new incarnation has no carriers")) return 1;
    queue.awaitingAcknowledgement = true;
    queue.sentCount = 1;
    queue.carryingCount = queue.carryingPackets.size() + 1;
    if (!check(!peer::prefix_confirmed(queue, ack, 140, 140), "invalid carrier count refused")) return 1;
    peer::InboundAckContext context{};
    std::uint16_t resolved = 0;
    ack = {};
    ack.ringInitialized = true;
    ack.outboundHeadPresent = true;
    ack.outboundHead = 20;
    ack.receiveHead = 4;
    if (!check(peer::resolve_ack_context(context, true, 20, 4, 4, resolved) && resolved == 4,
               "initial ACK context")) return 1;
    queue = {};
    queue.count = queue.sentCount = 1;
    queue.awaitingAcknowledgement = true;
    peer::record_carrier(queue, 132);
    if (!check(!peer::resolve_ack_context(context, true, 20, 4, 132, resolved),
               "old packet replay after local seven-bit wrap")) return 1;
    if (!check(peer::resolve_ack_context(context, true, 21, 4, 132, resolved)
               && resolved == 4 && !peer::prefix_confirmed(queue, ack, 132, resolved),
               "new remote packet with stale base cannot retire new prefix")) return 1;
    std::printf("PASS: %u acknowledgement checks including 319 native reader vectors\n", checks);
    return 0;
}
