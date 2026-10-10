#pragma once
#include <algorithm>
#include "../../../state/gameplay/definition.h"

namespace dawn::middleware::gameplay::peer {
inline void record_accepted_packet(state::gameplay::PeerLink& peer, std::uint16_t sequence) noexcept {
    constexpr auto modulus = state::gameplay::kPacketSequenceModulus;
    if (sequence >= modulus) return;
    if (!peer.ringInitialized) {
        peer.ringInitialized = true;
        peer.receiveHead = sequence;
        peer.received = {};
        peer.receivedCount = 0;
        return;
    }
    const auto advance = (sequence + modulus - peer.receiveHead) % modulus;
    if (!advance) return;
    if (advance >= state::gameplay::kPacketSequenceHalf) {
        const auto behind = (peer.receiveHead + modulus - sequence) % modulus;
        if (behind && behind <= peer.receivedCount && behind <= peer.received.size())
            peer.received[behind - 1] = true;
        return;
    }
    std::array<bool, state::gameplay::kAckHistory> shifted{};
    for (std::size_t index = 0; index < shifted.size(); ++index) {
        if (index + 1 < advance) continue;
        if (index + 1 == advance) { shifted[index] = true; continue; }
        const auto source = index - advance;
        shifted[index] = source < peer.receivedCount && peer.received[source];
    }
    peer.received = shifted;
    peer.receivedCount = static_cast<std::uint8_t>(std::min<std::size_t>(
        peer.received.size(), peer.receivedCount + advance));
    peer.receiveHead = sequence;
}
}
