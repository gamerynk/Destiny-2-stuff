#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "entity_slots.h"

namespace dawn::middleware::bap::activity_message::entity_authority {

inline constexpr std::uint32_t kAbandonMessageType = 26;
inline constexpr std::uint32_t kRequestPurgeMessageType = 27;
inline constexpr std::uint32_t kResetAcknowledgementMessageType = 29;
inline constexpr std::uint32_t kQueryPerBubbleMessageType = 31;
inline constexpr std::uint32_t kQueryResponseMessageType = 32;
inline constexpr std::uint32_t kAbdicateMessageType = 33;

inline constexpr std::uint8_t kSelectorWidth = 8;
inline constexpr std::uint8_t kReasonWidth = 3;
inline constexpr std::int32_t kReasonBias = 1;
inline constexpr std::size_t kAbandonBits =
    kSelectorWidth + entity_slots::kSlotCount + kReasonWidth;
inline constexpr std::size_t kRequestPurgeBits = kReasonWidth + entity_slots::kSlotCount;
inline constexpr std::size_t kAbdicateBits = kSelectorWidth + entity_slots::kSlotCount;
inline constexpr std::size_t kCorrelationSize = sizeof(std::uint32_t);

struct Release {
    entity_slots::EntitySlotMask mask{};
    std::uint8_t selector{};
    std::int32_t reason{};
    bool hasReason{};
};

struct QueryAnswer {
    entity_slots::EntitySlotMask mask{};
    std::uint32_t correlation{};
    std::uint8_t selector{};
    bool hasSelector{};
    bool hasMask{};
};

[[nodiscard]] bool parse_abandon(std::span<const std::byte> payload, Release& release) noexcept;

[[nodiscard]] bool parse_abdicate(std::span<const std::byte> payload, Release& release) noexcept;

// msg 27's mask is not byte aligned and this host takes no action on a purge request,
// so skip it
[[nodiscard]] bool parse_request_purge(std::span<const std::byte> payload,
                                       std::int32_t& reason) noexcept;

[[nodiscard]] bool parse_query_answer(std::uint32_t messageType,
                                      std::span<const std::byte> payload,
                                      QueryAnswer& answer) noexcept;

} // namespace dawn::middleware::bap::activity_message::entity_authority
