// the client sends msgs 26, 27, 29, 31, 32, 33 and never receives them; 26 and 33 return
// leased slots, the rest ask about host state this build doesn't keep, so only read

#include <algorithm>

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"
#include "entity_authority.h"

namespace dawn::middleware::bap::activity_message::entity_authority {
namespace {

[[nodiscard]] bool read_selector_and_mask(std::span<const std::byte> payload,
                                          Release& release) noexcept {
    constexpr std::size_t kSelectorSize = 1;
    if (payload.size() < kSelectorSize + entity_slots::kEncodedSize) {
        return false;
    }
    release.selector = std::to_integer<std::uint8_t>(payload.front());
    std::copy_n(payload.begin() + kSelectorSize, entity_slots::kEncodedSize, release.mask.begin());
    return true;
}

} // namespace

bool parse_abandon(std::span<const std::byte> payload, Release& release) noexcept {
    release = {};
    if (!read_selector_and_mask(payload, release)) {
        return false;
    }
    encoding::bits::Reader reader(payload);
    std::uint64_t stored = 0;
    if (!reader.skip(kSelectorWidth + entity_slots::kSlotCount)
        || !reader.read(kReasonWidth, stored)) {
        release = {};
        return false;
    }
    release.reason = static_cast<std::int32_t>(stored) - kReasonBias;
    release.hasReason = true;
    return true;
}

bool parse_abdicate(std::span<const std::byte> payload, Release& release) noexcept {
    release = {};
    if (!read_selector_and_mask(payload, release)) {
        release = {};
        return false;
    }
    return true;
}

bool parse_request_purge(std::span<const std::byte> payload, std::int32_t& reason) noexcept {
    reason = 0;
    encoding::bits::Reader reader(payload);
    std::uint64_t stored = 0;
    if (!reader.read(kReasonWidth, stored) || !reader.skip(entity_slots::kSlotCount)) {
        return false;
    }
    reason = static_cast<std::int32_t>(stored) - kReasonBias;
    return true;
}

bool parse_query_answer(std::uint32_t messageType,
                        std::span<const std::byte> payload,
                        QueryAnswer& answer) noexcept {
    answer = {};
    if (payload.size() < kCorrelationSize) {
        return false;
    }
    answer.correlation = encoding::read_u32_be(payload.first<kCorrelationSize>());
    if (messageType == kResetAcknowledgementMessageType) {
        return true;
    }

    std::size_t offset = kCorrelationSize;
    if (messageType == kQueryPerBubbleMessageType) {
        if (payload.size() < offset + 1) {
            answer = {};
            return false;
        }
        answer.selector = std::to_integer<std::uint8_t>(payload[offset]);
        answer.hasSelector = true;
        offset += 1;
    } else if (messageType != kQueryResponseMessageType) {
        answer = {};
        return false;
    }

    if (payload.size() < offset + entity_slots::kEncodedSize) {
        answer = {};
        return false;
    }
    std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                entity_slots::kEncodedSize,
                answer.mask.begin());
    answer.hasMask = true;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::entity_authority
