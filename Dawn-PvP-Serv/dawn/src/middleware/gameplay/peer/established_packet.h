#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/gameplay/definition.h"
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace dawn::middleware::gameplay::peer {

inline constexpr std::size_t kAckHistory = 8;
inline constexpr std::size_t kMaximumAckStatuses = 127;
inline constexpr std::size_t kPacketRingSize = 128;
inline constexpr std::size_t kLargeFragmentBytes = 32;
inline constexpr std::size_t kSmallFragmentBytes = 6;
inline constexpr std::size_t kMaximumRecords = 64;

struct AckState {
    std::uint16_t outboundHead{};
    bool outboundHeadPresent{};
    std::uint8_t headMinusCursor{};
    std::uint16_t receiveHead{};
    bool ringInitialized{};
    std::array<bool, kMaximumAckStatuses> received{};
    std::array<std::uint8_t, kMaximumAckStatuses> nativeStatuses{};
    // entries the wire form actually named; the rest of the array is unreported, not clear
    std::uint8_t reportedCount{};
    bool idle{};
    std::uint16_t delay{};
};

struct QueueRecord {
    std::uint16_t sequence{};
    bool shortFragment{};
    std::uint16_t bitCount{};
    std::array<std::byte, kLargeFragmentBytes> bytes{};
};

struct QueueRecords {
    std::array<QueueRecord, kMaximumRecords> records{};
    std::size_t count{};
};

struct EstablishedPacket {
    std::uint8_t connectionSequenceLow2{};
    AckState ack{};
    QueueRecords large{};
    QueueRecords small{};
    std::size_t externalBitOffset{};
    bool hasExternal{};
};

[[nodiscard]] bool decode_established(std::span<const std::byte> payload,
                                      bool expectExternal,
                                      EstablishedPacket& output) noexcept;

enum class ExternalTailResult { accepted_absent, present_unsupported, malformed };
[[nodiscard]] ExternalTailResult validate_absent_external_tail(
    std::span<const std::byte> payload, std::size_t bitOffset) noexcept;

[[nodiscard]] bool write_head_and_ack(encoding::bits::Writer& writer,
                                      std::uint8_t connectionSequenceLow2,
                                      const AckState& ack) noexcept;

// older packets need a named received status; an expired or unreported packet is not proof
// of delivery
[[nodiscard]] bool acknowledgement_covers(const AckState& ack, std::uint16_t sentSequence,
                                           std::uint16_t resolvedBase) noexcept;

[[nodiscard]] bool write_empty_queue(encoding::bits::Writer& writer) noexcept;

// each record names its sequence absolutely; delta forms desync if a record is dropped
[[nodiscard]] bool write_queue(encoding::bits::Writer& writer,
                               const state::gameplay::OutboundQueue& queue,
                               std::size_t recordCount) noexcept;

[[nodiscard]] bool enqueue_message(state::gameplay::OutboundQueue& queue,
                                   std::uint8_t id,
                                   std::uint32_t declaredSize,
                                   std::span<const std::byte> body,
                                   std::size_t bodyBits) noexcept;

[[nodiscard]] bool write_absent_filler(encoding::bits::Writer& writer) noexcept;

} // namespace dawn::middleware::gameplay::peer
