#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../state/gameplay/definition.h"
#include "established_packet.h"

namespace dawn::middleware::gameplay::peer {

struct AssembledMessage {
    std::array<std::byte, state::gameplay::kReassemblyCapacity> bytes{};
    std::size_t bitCount{};
    std::uint8_t id{};
    std::uint32_t declaredSize{};
    std::size_t bodyBitOffset{};
};

// a record outside the buffered window is dropped; the sender retransmits once it moves, so
// a queue that never drains shows up only as a rising drop count
std::size_t accept_records(const QueueRecords& records,
                           state::gameplay::ReliableQueue& queue) noexcept;

[[nodiscard]] bool drain_message(state::gameplay::ReliableQueue& queue,
                                 AssembledMessage& output) noexcept;

} // namespace dawn::middleware::gameplay::peer
