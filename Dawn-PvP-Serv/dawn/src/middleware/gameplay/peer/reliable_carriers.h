#pragma once
#include "established_packet.h"

namespace dawn::middleware::gameplay::peer {
inline void record_carrier(state::gameplay::OutboundQueue& queue, std::uint16_t sequence) noexcept {
    if (!queue.awaitingAcknowledgement || !queue.sentCount
        || sequence >= state::gameplay::kPacketSequenceModulus) return;
    std::size_t retained = 0;
    for (std::size_t index = 0; index < queue.carryingCount && index < queue.carryingPackets.size(); ++index) {
        const auto carrier = queue.carryingPackets[index];
        const auto age = (sequence + state::gameplay::kPacketSequenceModulus - carrier)
                         % state::gameplay::kPacketSequenceModulus;
        if (age && age < kPacketRingSize / 2)
            queue.carryingPackets[retained++] = carrier;
    }
    if (retained == queue.carryingPackets.size()) {
        for (std::size_t index = 1; index < retained; ++index)
            queue.carryingPackets[index - 1] = queue.carryingPackets[index];
        --retained;
    }
    queue.carryingPackets[retained++] = sequence;
    queue.carryingCount = retained;
}

[[nodiscard]] inline bool prefix_confirmed(const state::gameplay::OutboundQueue& queue,
                                           const AckState& ack, std::uint16_t latestSent,
                                           std::uint16_t resolvedBase) noexcept {
    if (!queue.awaitingAcknowledgement || !queue.sentCount
        || queue.carryingCount > queue.carryingPackets.size()) return false;
    for (std::size_t index = 0; index < queue.carryingCount; ++index) {
        const auto carrier = queue.carryingPackets[index];
        const auto age = (latestSent + state::gameplay::kPacketSequenceModulus - carrier)
                         % state::gameplay::kPacketSequenceModulus;
        if (age < kPacketRingSize / 2 && acknowledgement_covers(ack, carrier, resolvedBase)) return true;
    }
    return false;
}
}
