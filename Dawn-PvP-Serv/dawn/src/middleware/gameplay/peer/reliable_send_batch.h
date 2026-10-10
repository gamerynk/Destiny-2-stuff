#pragma once
#include <algorithm>
#include "../../../state/gameplay/definition.h"

namespace dawn::middleware::gameplay::peer {
// sixteen 32-byte records plus framing fit the 1024-byte control packet and the receiver's
// 32-slot reliable window; native entity payloads keep their one-record budget
inline constexpr std::size_t kControlFragmentBatch = 16;
[[nodiscard]] constexpr std::size_t reliable_send_batch(
    const state::gameplay::OutboundQueue& queue, bool nativeEntityStream) noexcept {
    if (queue.awaitingAcknowledgement) return queue.sentCount;
    return (std::min)(queue.count, nativeEntityStream ? std::size_t{1} : kControlFragmentBatch);
}
}
