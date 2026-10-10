// msg 19 targets index a 7,763-record table read unchecked, so a bad index crashes; rows
// 795, 4690, 5375 hold type -1 and are the same risk. Selector 0x80807216 -> 0x808072A5

#include <algorithm>
#include <climits>

#include "../../encoding/bit_reader.h"
#include "incident.h"

namespace dawn::middleware::bap::activity_message::incident {
namespace {

[[nodiscard]] bool target_allowed(std::uint32_t target, Verdict& verdict) noexcept {
    if (target > kTargetMaximum) {
        verdict = Verdict::targetOutOfRange;
        return false;
    }
    if (std::find(kPoisonTargets.begin(), kPoisonTargets.end(), target) != kPoisonTargets.end()) {
        verdict = Verdict::targetPoisoned;
        return false;
    }
    return true;
}

} // namespace

const char* verdict_name(Verdict verdict) noexcept {
    switch (verdict) {
    case Verdict::accepted:
        return "accepted";
    case Verdict::truncated:
        return "truncated";
    case Verdict::targetOutOfRange:
        return "target_out_of_range";
    case Verdict::targetPoisoned:
        return "target_poisoned";
    case Verdict::tooManyTargets:
        return "too_many_targets";
    case Verdict::payloadTooLong:
        return "payload_too_long";
    case Verdict::selectorTooLong:
        return "selector_too_long";
    }
    return "unknown";
}

Verdict validate(std::span<const std::byte> payload, Incident& parsed) noexcept {
    parsed = {};
    encoding::bits::Reader reader(payload);

    std::uint64_t field = 0;
    if (!reader.read(kTargetWidth, field)) {
        return Verdict::truncated;
    }
    parsed.primaryTarget = static_cast<std::uint32_t>(field);
    Verdict verdict = Verdict::accepted;
    if (!target_allowed(parsed.primaryTarget, verdict)) {
        return verdict;
    }

    if (!reader.read(kExtraCountWidth, field)) {
        return Verdict::truncated;
    }
    parsed.extraTargetCount = static_cast<std::uint32_t>(field);
    if (parsed.extraTargetCount > kExtraTargetMaximum) {
        return Verdict::tooManyTargets;
    }
    for (std::uint32_t index = 0; index < parsed.extraTargetCount; ++index) {
        if (!reader.read(kTargetWidth, field)) {
            return Verdict::truncated;
        }
        parsed.extraTargets[index] = static_cast<std::uint32_t>(field);
        if (!target_allowed(parsed.extraTargets[index], verdict)) {
            return verdict;
        }
    }

    if (!reader.read(kSelectorPresenceWidth, field)) {
        return Verdict::truncated;
    }
    if (field != 0) {
        parsed.hasCompressedSelector = true;
        if (!reader.read(kSelectorLengthWidth, field)) return Verdict::truncated;
        if (field > kSelectorMaximum) return Verdict::selectorTooLong;
        if (!reader.skip(static_cast<std::size_t>(field) * CHAR_BIT)) return Verdict::truncated;
    }

    if (!reader.read(kOptionalPresenceWidth, field)) {
        return Verdict::truncated;
    }
    if (field != 0 && !reader.skip(kOptionalFieldWidth)) {
        return Verdict::truncated;
    }

    if (!reader.read(kPayloadLengthWidth, field)) {
        return Verdict::truncated;
    }
    parsed.payloadLength = static_cast<std::uint32_t>(field);
    if (parsed.payloadLength > kPayloadMaximum) {
        return Verdict::payloadTooLong;
    }
    if (reader.remaining_bits() < static_cast<std::size_t>(parsed.payloadLength) * CHAR_BIT) {
        return Verdict::truncated;
    }
    for (std::uint32_t index = 0; index < parsed.payloadLength; ++index) {
        if (!reader.read(CHAR_BIT, field)) {
            return Verdict::truncated;
        }
        parsed.payload[index] = static_cast<std::byte>(field);
    }
    parsed.hasPayload = true;
    return Verdict::accepted;
}

} // namespace dawn::middleware::bap::activity_message::incident
