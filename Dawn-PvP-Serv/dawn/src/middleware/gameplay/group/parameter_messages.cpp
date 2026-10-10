#include "parameter_messages.h"

#include <array>
#include <cstddef>
#include <cstring>

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::group {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kFlagWidth = 1;
constexpr std::uint8_t kRootBitWidth = 1;
constexpr std::uint8_t kGoalStepWidth = 6;
constexpr std::uint8_t kGoalReasonWidth = 9;
constexpr std::uint8_t kGoalSequenceWidth = 8;
constexpr std::uint8_t kCurrentReasonWidth = 4;
constexpr std::uint8_t kCurrentActivityIndexWidth = 12;
constexpr std::uint8_t kCurrentNestedCountWidth = 5;
constexpr std::uint8_t kCurrentFixedValueWidth = 8;
constexpr std::uint8_t kHostIdWidth = 64;
constexpr std::uint8_t kAddressWidth = 32;
constexpr std::uint8_t kPortWidth = 16;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFF;
constexpr std::uint8_t kGroupSize = 16;
constexpr std::uint8_t kGroupCount = 2;
constexpr std::uint64_t kParameterMaskBits = 0x1FFFFFF;
constexpr std::uint8_t kProposalMemberIndexWidth = 6;
constexpr std::size_t kActivitySelectionSlotCapacity = 9;
constexpr std::size_t kActivitySelectionResponseBucketCapacity = 20;
constexpr std::size_t kActivitySelectionResponseSlotCapacity = 9;

struct ProposalWalk final {
    std::size_t memberIndexBitOffset{};
    std::uint8_t memberIndex{kUnspecifiedParameterMember};
    bool memberIndexPresent{};
};

[[nodiscard]] bool read_presence(bits::Reader& reader, bool& present) noexcept {
    std::uint64_t value = 0;
    if (!reader.read(kFlagWidth, value)) {
        return false;
    }
    present = value != 0;
    return true;
}

[[nodiscard]] bool skip_optional(bits::Reader& reader, std::size_t width) noexcept {
    bool present = false;
    return read_presence(reader, present) && (!present || reader.skip(width));
}

// follows tag-reflection root 0x80808719 directly, not the older BAP field-one layout
[[nodiscard]] bool skip_activity_descriptor(bits::Reader& reader) noexcept {
    if (!reader.skip(4 + 12 + 12 + 9) || !skip_optional(reader, 64)) {
        return false;
    }
    bool hasSkulls = false;
    std::uint64_t skullCount = 0;
    if (!read_presence(reader, hasSkulls)
        || (hasSkulls
            && (!reader.read(5, skullCount) || skullCount > 16
                || !reader.skip(static_cast<std::size_t>(skullCount) * (2 + 7))))) {
        return false;
    }

    if (!skip_optional(reader, 8) || !skip_optional(reader, 32)
        || !skip_optional(reader, 32) || !reader.skip(1)) {
        return false;
    }

    std::uint64_t packageCount = 0;
    if (!reader.read(1, packageCount)
        || (packageCount != 0 && !reader.skip(40 * kByteBits))) {
        return false;
    }

    std::uint64_t referenceCount = 0;
    if (!reader.read(1, referenceCount)
        || !reader.skip(static_cast<std::size_t>(referenceCount) * 96)) {
        return false;
    }

    std::uint64_t pairCount = 0;
    return reader.read(6, pairCount) && pairCount <= 32
           && reader.skip(static_cast<std::size_t>(pairCount) * 13) && reader.skip(3);
}

[[nodiscard]] bool skip_activity_selection_slot(bits::Reader& reader) noexcept {
    return reader.skip(8) && skip_activity_descriptor(reader);
}

[[nodiscard]] bool skip_activity_selection_state(bits::Reader& reader) noexcept {
    for (std::size_t index = 0; index < kActivitySelectionSlotCapacity; ++index) {
        bool present = false;
        if (!read_presence(reader, present)
            || (present && !skip_activity_selection_slot(reader))) {
            return false;
        }
    }
    return reader.skip(4);
}

[[nodiscard]] bool skip_activity_selection_launch(bits::Reader& reader) noexcept {
    if (!reader.skip(8) || !reader.skip(8) || !skip_optional(reader, 3)
        || !skip_optional(reader, 3) || !reader.skip(8) || !reader.skip(8)) {
        return false;
    }

    bool hasDescriptor = false;
    if (!read_presence(reader, hasDescriptor)) {
        return false;
    }
    if (hasDescriptor) {
        if (!skip_activity_descriptor(reader)) {
            return false;
        }
    }

    return skip_optional(reader, 6) && skip_optional(reader, 6)
           && skip_optional(reader, 128 * kByteBits);
}

[[nodiscard]] bool walk_activity_selection_proposal(bits::Reader& reader,
                                                    ProposalWalk& output) noexcept {
    const std::size_t startBits = reader.remaining_bits();
    bool hasMemberIndex = false;
    if (!read_presence(reader, hasMemberIndex)) {
        return false;
    }
    if (hasMemberIndex) {
        output.memberIndexBitOffset = startBits - reader.remaining_bits();
        std::uint64_t encoded = 0;
        if (!reader.read(kProposalMemberIndexWidth, encoded)) {
            return false;
        }
        output.memberIndexPresent = true;
        output.memberIndex = encoded == 0
                                 ? kUnspecifiedParameterMember
                                 : static_cast<std::uint8_t>(encoded - 1U);
    }

    bool hasSelectionState = false;
    if (!read_presence(reader, hasSelectionState)
        || (hasSelectionState && !skip_activity_selection_state(reader))) {
        return false;
    }
    bool hasLaunchState = false;
    return read_presence(reader, hasLaunchState)
           && (!hasLaunchState || skip_activity_selection_launch(reader));
}

[[nodiscard]] bool skip_activity_selection_response_leaf(bits::Reader& reader) noexcept {
    return reader.skip(8 + 7 + 3 * (2 + 8));
}

[[nodiscard]] bool skip_activity_selection_responses_state(bits::Reader& reader) noexcept {
    std::uint64_t outerCount = 0;
    if (!reader.read(1, outerCount)) {
        return false;
    }
    for (std::uint64_t outer = 0; outer < outerCount; ++outer) {
        for (std::size_t bucket = 0; bucket < kActivitySelectionResponseBucketCapacity;
             ++bucket) {
            std::uint64_t bucketCount = 0;
            if (!reader.read(1, bucketCount)) {
                return false;
            }
            for (std::uint64_t entry = 0; entry < bucketCount; ++entry) {
                for (std::size_t slot = 0; slot < kActivitySelectionResponseSlotCapacity;
                     ++slot) {
                    std::uint64_t slotCount = 0;
                    if (!reader.read(1, slotCount)) {
                        return false;
                    }
                    for (std::uint64_t value = 0; value < slotCount; ++value) {
                        if (!skip_activity_selection_response_leaf(reader)) {
                            return false;
                        }
                    }
                }
            }
        }

        bool hasReceipts = false;
        if (!read_presence(reader, hasReceipts)) {
            return false;
        }
        if (hasReceipts) {
            for (std::size_t slot = 0; slot < kActivitySelectionResponseSlotCapacity; ++slot) {
                std::uint64_t receiptCount = 0;
                if (!reader.read(1, receiptCount)
                    || (receiptCount != 0 && !reader.skip(8 + 24))) {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool walk_activity_selection_responses_proposal(bits::Reader& reader,
                                                              ProposalWalk& output) noexcept {
    const std::size_t startBits = reader.remaining_bits();
    bool hasResponses = false;
    if (!read_presence(reader, hasResponses)
        || (hasResponses && !skip_activity_selection_responses_state(reader))) {
        return false;
    }

    bool hasMemberIndex = false;
    if (!read_presence(reader, hasMemberIndex)) {
        return false;
    }
    if (!hasMemberIndex) {
        return true;
    }
    output.memberIndexBitOffset = startBits - reader.remaining_bits();
    std::uint64_t encoded = 0;
    if (!reader.read(kProposalMemberIndexWidth, encoded)) {
        return false;
    }
    output.memberIndexPresent = true;
    output.memberIndex = encoded == 0
                             ? kUnspecifiedParameterMember
                             : static_cast<std::uint8_t>(encoded - 1U);
    return true;
}

using ProposalWalker = bool (*)(bits::Reader&, ProposalWalk&) noexcept;

// ID 39 supplies FC/EC via the root-present wrapper; replaying it inside an F8/E9 array
// row would shift every following member
[[nodiscard]] bool capture_parameter_body(bits::Reader& reader,
                                          ProposalWalker walk,
                                          EncodedParameterBody& output,
                                          std::size_t* failureConsumedBits) noexcept {
    bits::Reader probe = reader;
    const std::size_t before = probe.remaining_bits();
    if (failureConsumedBits != nullptr) {
        *failureConsumedBits = 0;
    }
    bool rootPresent = false;
    if (!read_presence(probe, rootPresent)) {
        if (failureConsumedBits != nullptr) {
            *failureConsumedBits = before - probe.remaining_bits();
        }
        return false;
    }
    if (!rootPresent) {
        bool consumedPresent = true;
        EncodedParameterBody empty{};
        if (!read_presence(reader, consumedPresent) || consumedPresent) {
            if (failureConsumedBits != nullptr) {
                *failureConsumedBits = before - reader.remaining_bits();
            }
            return false;
        }
        output = empty;
        return true;
    }

    const std::size_t descriptorBefore = probe.remaining_bits();
    ProposalWalk metadata{};
    if (!walk(probe, metadata)) {
        if (failureConsumedBits != nullptr) {
            *failureConsumedBits = before - probe.remaining_bits();
        }
        return false;
    }
    const std::size_t bitCount = descriptorBefore - probe.remaining_bits();
    if (bitCount == 0 || bitCount > kEncodedParameterBodyCapacity * kByteBits) {
        if (failureConsumedBits != nullptr) {
            *failureConsumedBits = bitCount;
        }
        return false;
    }

    EncodedParameterBody captured{};
    bits::Writer writer(captured.bytes);
    bool consumedPresent = false;
    if (!read_presence(reader, consumedPresent) || !consumedPresent) {
        if (failureConsumedBits != nullptr) {
            *failureConsumedBits = before - reader.remaining_bits();
        }
        return false;
    }
    std::size_t remaining = bitCount;
    while (remaining != 0) {
        const auto width = static_cast<std::uint8_t>(remaining < 64 ? remaining : 64);
        std::uint64_t value = 0;
        if (!reader.read(width, value) || !writer.write(value, width)) {
            if (failureConsumedBits != nullptr) {
                *failureConsumedBits = bitCount - remaining;
            }
            return false;
        }
        remaining -= width;
    }
    std::size_t written = 0;
    if (!writer.finish(written)) {
        return false;
    }
    captured.bitCount = bitCount;
    captured.memberIndexBitOffset = metadata.memberIndexBitOffset;
    captured.memberIndex = metadata.memberIndex;
    captured.memberIndexPresent = metadata.memberIndexPresent;
    captured.present = true;
    output = captured;
    return true;
}

// canonicalize any present producer scalar (incl. the native -1 sentinel) to the authenticated
// row; replaying the sentinel unchanged would overwrite the F8/E9 initializer
[[nodiscard]] bool write_parameter_body_bits(bits::Writer& writer,
                                             const EncodedParameterBody& body,
                                             std::uint8_t memberIndex) noexcept {
    if (!body.present || body.bitCount == 0
        || body.bitCount > body.bytes.size() * kByteBits
        || memberIndex >= kParameterMemberCapacity
        || (body.memberIndexPresent
            && body.memberIndexBitOffset + kProposalMemberIndexWidth > body.bitCount)) {
        return false;
    }
    const std::size_t byteCount = (body.bitCount + kByteBits - 1) / kByteBits;
    bits::Reader reader({body.bytes.data(), byteCount});
    std::size_t consumed = 0;
    while (consumed < body.bitCount) {
        if (body.memberIndexPresent && consumed == body.memberIndexBitOffset) {
            std::uint64_t ignored = 0;
            if (!reader.read(kProposalMemberIndexWidth, ignored)
                || !writer.write(memberIndex + 1U, kProposalMemberIndexWidth)) {
                return false;
            }
            consumed += kProposalMemberIndexWidth;
            continue;
        }
        std::size_t available = body.bitCount - consumed;
        if (body.memberIndexPresent && consumed < body.memberIndexBitOffset) {
            available = body.memberIndexBitOffset - consumed;
        }
        const auto width = static_cast<std::uint8_t>(available < 64 ? available : 64);
        std::uint64_t value = 0;
        if (!reader.read(width, value) || !writer.write(value, width)) {
            return false;
        }
        consumed += width;
    }
    return true;
}

[[nodiscard]] bool write_activity_selection_update(
    bits::Writer& writer,
    std::span<const EncodedParameterBody* const> members) noexcept {
    if (members.size() != kParameterMemberCapacity
        || !writer.write(1U, kRootBitWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        const EncodedParameterBody* const body = members[index];
        const bool present = body != nullptr && body->present;
        if (!writer.write(present ? 1U : 0U, kFlagWidth)) {
            return false;
        }
        if (present) {
            if ((body->memberIndexPresent
                 && body->memberIndex != kUnspecifiedParameterMember
                 && body->memberIndex != static_cast<std::uint8_t>(index))
                || !write_parameter_body_bits(writer, *body, static_cast<std::uint8_t>(index))) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool write_activity_selection_responses_update(
    bits::Writer& writer,
    std::span<const EncodedParameterBody* const> members) noexcept {
    if (members.size() != kParameterMemberCapacity
        || !writer.write(1U, kRootBitWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        const EncodedParameterBody* const body = members[index];
        if (body == nullptr || !body->present) {
            if (!writer.write(0U, kFlagWidth) || !writer.write(0U, kFlagWidth)) {
                return false;
            }
            continue;
        }
        if ((body->memberIndexPresent
             && body->memberIndex != kUnspecifiedParameterMember
             && body->memberIndex != static_cast<std::uint8_t>(index))
            || !write_parameter_body_bits(writer, *body, static_cast<std::uint8_t>(index))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
write_world_controller_goal_data(bits::Writer& writer,
                                 const WorldControllerGoalDataParameter& body) noexcept {
    constexpr std::int32_t kMinimumBiasedValue = -1;
    constexpr std::int32_t kMaximumGoalStep = 62;
    constexpr std::int32_t kMaximumGoalReason = 510;
    if (body.goalStep < kMinimumBiasedValue || body.goalStep > kMaximumGoalStep
        || body.goalReason < kMinimumBiasedValue || body.goalReason > kMaximumGoalReason) {
        return false;
    }
    return writer.write(static_cast<std::uint64_t>(body.goalStep + 1), kGoalStepWidth)
           && writer.write(static_cast<std::uint64_t>(body.goalReason + 1), kGoalReasonWidth)
           && writer.write(body.acceptedSequence, kGoalSequenceWidth);
}

[[nodiscard]] bool write_current_activity(bits::Writer& writer,
                                           const CurrentActivityParameter& body) noexcept {
    if (!writer.write(body.present ? 1U : 0U, kRootBitWidth)) {
        return false;
    }
    if (!body.present) {
        return true;
    }
    constexpr std::int32_t kMinimumReason = -1;
    constexpr std::int32_t kMaximumReason = 14;
    constexpr std::int32_t kMinimumActivityIndex = -1;
    constexpr std::int32_t kMaximumActivityIndex = 4094;
    if (body.reason < kMinimumReason || body.reason > kMaximumReason
        || body.sourceActivityIndex < kMinimumActivityIndex
        || body.sourceActivityIndex > kMaximumActivityIndex
        || body.activityIndex < kMinimumActivityIndex
        || body.activityIndex > kMaximumActivityIndex) {
        return false;
    }

    return writer.write(static_cast<std::uint64_t>(body.reason + 1), kCurrentReasonWidth)
           && writer.write(static_cast<std::uint64_t>(body.sourceActivityIndex + 1),
                           kCurrentActivityIndexWidth)
           && writer.write(static_cast<std::uint64_t>(body.activityIndex + 1),
                           kCurrentActivityIndexWidth)
           && writer.write(0U, kFlagWidth) && writer.write(0U, kFlagWidth)
           && writer.write(0U, kFlagWidth)
           && writer.write(0U, kCurrentNestedCountWidth)
           && writer.write(body.fixedValue, kCurrentFixedValueWidth)
           && writer.write(0U, kFlagWidth) && writer.write(0U, kFlagWidth)
           && writer.write(0U, kFlagWidth) && writer.write(0U, kFlagWidth)
           && writer.write(0U, kFlagWidth)
           && writer.write(0U, kFlagWidth) && writer.write(0U, kFlagWidth)
           && writer.write(0U, kFlagWidth);
}

[[nodiscard]] std::uint64_t group_presence(std::uint64_t mask) noexcept {
    std::uint64_t groups = 0;
    for (std::uint8_t group = 0; group < kGroupCount; ++group) {
        const std::uint64_t span = (mask >> (group * kGroupSize)) & ((1ULL << kGroupSize) - 1);
        if ((span & kParameterMaskBits) != 0) {
            groups |= 1ULL << group;
        }
    }
    return groups;
}

[[nodiscard]] bool write_activity_host(bits::Writer& writer,
                                       const ActivityHostParameter& body) noexcept {
    std::array<std::byte, sizeof(std::uint32_t)> mask{};
    for (std::size_t index = 0; index < mask.size(); ++index) {
        const unsigned shift = static_cast<unsigned>(index) * kByteBits;
        mask[index] = static_cast<std::byte>((body.memberMask >> shift) & kByteMask);
    }
    return bits::write_raw_u64(writer, body.selectionId) && writer.write(body.hostId, kHostIdWidth)
           && bits::write_raw(writer, mask) && writer.write(body.address, kAddressWidth)
           && writer.write(body.port, kPortWidth);
}

// a clear root bit is a complete body for both tag-reflection parameters
[[nodiscard]] bool write_parameter_body(bits::Writer& writer,
                                        std::uint8_t parameter,
                                        const ParameterUpdate& update) noexcept {
    if (parameter == static_cast<std::uint8_t>(Parameter::worldControllerGoalData)) {
        return write_world_controller_goal_data(writer, update.worldControllerGoalData);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::activitySelection)) {
        return write_activity_selection_update(writer, update.activitySelections);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::activitySelectionResponses)) {
        return write_activity_selection_responses_update(writer,
                                                         update.activitySelectionResponses);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::currentActivity)) {
        return write_current_activity(writer, update.currentActivity);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::previousActivity)) {
        return write_current_activity(writer, update.previousActivity);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::publicSessionReservations)) {
        return writer.write(0U, kRootBitWidth);
    }
    if (parameter == static_cast<std::uint8_t>(Parameter::activityHost)) {
        return write_activity_host(writer, update.activityHost);
    }
    return false;
}

[[nodiscard]] bool write_mask_bits(bits::Writer& writer,
                                   std::uint64_t mask,
                                   std::uint64_t groups,
                                   bool carriesBodies,
                                   const ParameterUpdate& update) noexcept {
    for (std::uint8_t parameter = 0; parameter < kParameterCount; ++parameter) {
        if ((groups & (1ULL << (parameter / kGroupSize))) == 0) {
            continue;
        }
        const bool named = ((mask >> parameter) & 1ULL) != 0;
        if (!writer.write(named ? 1U : 0U, kFlagWidth)) {
            return false;
        }
        // a body must follow its own bit; the consumer reads them interleaved, not in two runs
        if (named && carriesBodies && !write_parameter_body(writer, parameter, update)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool read_parameter_request(bits::Reader& reader, ParameterRequestHeader& output) noexcept {
    ParameterRequestHeader candidate{};
    std::uint64_t mode = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId) || !reader.read(kFlagWidth, mode)
        || !bits::read_raw_u64(reader, candidate.requestedMask)) {
        return false;
    }
    candidate.modeFlag = mode != 0;
    output = candidate;
    return true;
}

bool read_activity_selection_proposal(bits::Reader& reader,
                                      EncodedParameterBody& output,
                                      std::size_t* failureConsumedBits) noexcept {
    return capture_parameter_body(
        reader, walk_activity_selection_proposal, output, failureConsumedBits);
}

bool read_activity_selection_responses_proposal(bits::Reader& reader,
                                                EncodedParameterBody& output,
                                                std::size_t* failureConsumedBits) noexcept {
    return capture_parameter_body(
        reader, walk_activity_selection_responses_proposal, output, failureConsumedBits);
}

bool validate_parameter_proposal_member(const EncodedParameterBody& body,
                                        std::uint8_t memberIndex) noexcept {
    if (memberIndex >= kParameterMemberCapacity) {
        return false;
    }
    return !body.present || !body.memberIndexPresent
           || body.memberIndex == kUnspecifiedParameterMember || body.memberIndex == memberIndex;
}

bool same_parameter_body(const EncodedParameterBody& left,
                         const EncodedParameterBody& right) noexcept {
    if (left.present != right.present || left.bitCount != right.bitCount
        || left.memberIndexPresent != right.memberIndexPresent
        || left.memberIndex != right.memberIndex
        || left.memberIndexBitOffset != right.memberIndexBitOffset) {
        return false;
    }
    if (!left.present) {
        return true;
    }
    const std::size_t byteCount = (left.bitCount + kByteBits - 1) / kByteBits;
    return std::memcmp(left.bytes.data(), right.bytes.data(), byteCount) == 0;
}

bool write_parameter_update(bits::Writer& writer, const ParameterUpdate& body) noexcept {
    const std::uint64_t released = body.releasedMask & kParameterMaskBits;
    const std::uint64_t carried = body.carriedMask & kParameterMaskBits;
    if ((carried & ~kEncodableParameters) != 0) {
        return false;
    }
    return writer.write(body.resetFlag ? 1U : 0U, kFlagWidth)
           && bits::write_raw_u64(writer, body.sessionId)
           && writer.write(group_presence(released), kGroupCount)
           && writer.write(group_presence(carried), kGroupCount)
           && write_mask_bits(writer, released, group_presence(released), false, body)
           && write_mask_bits(writer, carried, group_presence(carried), true, body);
}

bool requests(std::uint64_t mask, std::uint8_t parameter) noexcept {
    if (parameter >= kParameterCount) {
        return false;
    }
    return ((mask >> parameter) & 1U) != 0;
}

} // namespace dawn::middleware::gameplay::group
