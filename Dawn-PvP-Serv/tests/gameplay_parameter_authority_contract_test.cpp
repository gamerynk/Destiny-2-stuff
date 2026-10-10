#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <span>

#include "middleware/encoding/bit_raw.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/gameplay/group/parameter_messages.h"

namespace bits = dawn::middleware::encoding::bits;
namespace group = dawn::middleware::gameplay::group;

namespace {

[[nodiscard]] bool expect(bits::Reader& reader,
                          std::uint8_t width,
                          std::uint64_t expected,
                          const char* field) noexcept {
    std::uint64_t actual = 0;
    if (reader.read(width, actual) && actual == expected) {
        return true;
    }
    std::fprintf(stderr,
                 "%s mismatch: actual=0x%llX expected=0x%llX\n",
                 field,
                 static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    return false;
}

[[nodiscard]] bool write_zeros(bits::Writer& writer, std::size_t width) noexcept {
    while (width != 0) {
        const auto take = static_cast<std::uint8_t>(width < 64 ? width : 64);
        if (!writer.write(0, take)) {
            return false;
        }
        width -= take;
    }
    return true;
}

[[nodiscard]] bool write_captured(bits::Writer& writer,
                                  const group::EncodedParameterBody& body) noexcept {
    bits::Reader reader(
        std::span<const std::byte>(body.bytes.data(), (body.bitCount + 7) / 8),
        body.bitCount);
    std::size_t remaining = body.bitCount;
    while (remaining != 0) {
        const auto take = static_cast<std::uint8_t>(remaining < 64 ? remaining : 64);
        std::uint64_t value = 0;
        if (!reader.read(take, value) || !writer.write(value, take)) {
            return false;
        }
        remaining -= take;
    }
    return true;
}

[[nodiscard]] bool write_descriptor_fixture(bits::Writer& writer,
                                            std::uint8_t skullCount,
                                            std::uint8_t pairCount) noexcept {
    return write_zeros(writer, 4 + 12 + 12 + 9)
           && writer.write(1, 1) && write_zeros(writer, 64)
           && writer.write(1, 1) && writer.write(skullCount, 5)
           && write_zeros(writer, static_cast<std::size_t>(skullCount) * 9)
           && writer.write(1, 1) && write_zeros(writer, 8)
           && writer.write(1, 1) && write_zeros(writer, 32)
           && writer.write(1, 1) && write_zeros(writer, 32)
           && writer.write(0, 1) && writer.write(1, 1) && write_zeros(writer, 320)
           && writer.write(1, 1) && write_zeros(writer, 96)
           && writer.write(pairCount, 6)
           && write_zeros(writer, static_cast<std::size_t>(pairCount) * 13)
           && write_zeros(writer, 3);
}

[[nodiscard]] bool write_full_selection_fixture(bits::Writer& writer,
                                                 std::uint8_t memberIndex,
                                                 std::uint8_t skullCount,
                                                 std::uint8_t pairCount) noexcept {
    if (!writer.write(1, 1)
        || !writer.write(1, 1) || !writer.write(memberIndex + 1U, 6)
        || !writer.write(1, 1)) {
        return false;
    }
    for (std::size_t slot = 0; slot < 9; ++slot) {
        const bool present = slot == 0;
        if (!writer.write(present ? 1U : 0U, 1)) {
            return false;
        }
        if (present
            && (!write_zeros(writer, 8)
                || !write_descriptor_fixture(writer, skullCount, pairCount))) {
            return false;
        }
    }
    return write_zeros(writer, 4) && writer.write(1, 1) && write_zeros(writer, 8 + 8)
           && writer.write(1, 1) && write_zeros(writer, 3)
           && writer.write(1, 1) && write_zeros(writer, 3) && write_zeros(writer, 8 + 8)
           && writer.write(1, 1)
           && write_descriptor_fixture(writer, skullCount, pairCount)
           && writer.write(1, 1) && write_zeros(writer, 6)
           && writer.write(1, 1) && write_zeros(writer, 6)
           && writer.write(1, 1) && write_zeros(writer, 128 * 8);
}

} // namespace

int main() {
    constexpr std::uint8_t kAuthoritativeMember = 1;
    constexpr std::uint8_t kWireMember = kAuthoritativeMember;
    constexpr std::uint64_t kSessionId = 0x1122334455667788ULL;
    constexpr std::uint64_t kAuthorityMask =
        (std::uint64_t{1} << static_cast<std::uint8_t>(group::Parameter::activitySelection))
        | (std::uint64_t{1}
           << static_cast<std::uint8_t>(group::Parameter::activitySelectionResponses));

    std::array<std::byte, 8> requestStorage{};
    bits::Writer requestWriter(requestStorage);
    if (!requestWriter.write(1, 1)
        || !requestWriter.write(1, 1) || !requestWriter.write(kWireMember + 1U, 6)
        || !requestWriter.write(0, 1) || !requestWriter.write(0, 1)
        || !requestWriter.write(1, 1)
        || !requestWriter.write(0, 1) || !requestWriter.write(1, 1)
        || !requestWriter.write(kWireMember + 1U, 6)) {
        std::fputs("proposal fixture failed to encode\n", stderr);
        return 1;
    }
    std::size_t requestBytes = 0;
    if (!requestWriter.finish(requestBytes) || requestWriter.bit_count() != 19) {
        std::fputs("proposal fixture size mismatch\n", stderr);
        return 2;
    }

    bits::Reader requestReader(
        std::span<const std::byte>(requestStorage.data(), requestBytes), 19);
    group::EncodedParameterBody selection{};
    group::EncodedParameterBody responses{};
    if (!group::read_activity_selection_proposal(requestReader, selection)
        || !group::read_activity_selection_responses_proposal(requestReader, responses)
        || selection.bitCount != 9 || responses.bitCount != 8
        || !selection.memberIndexPresent || !responses.memberIndexPresent
        || selection.memberIndex != kWireMember || responses.memberIndex != kWireMember) {
        std::fputs("proposal roots did not split or decode exactly\n", stderr);
        return 3;
    }

    if (!group::validate_parameter_proposal_member(selection, kAuthoritativeMember)
        || !group::validate_parameter_proposal_member(responses, kAuthoritativeMember)
        || group::validate_parameter_proposal_member(selection, 0)
        || group::validate_parameter_proposal_member(responses, 0)) {
        std::fputs("proposal member validation failed\n", stderr);
        return 4;
    }

    std::array<std::byte, 32> fullRequestStorage{};
    bits::Writer fullRequestWriter(fullRequestStorage);
    if (!bits::write_raw_u64(fullRequestWriter, kSessionId)
        || !fullRequestWriter.write(0, 1)
        || !bits::write_raw_u64(fullRequestWriter, kAuthorityMask)
        || !fullRequestWriter.write(1, 1)
        || !fullRequestWriter.write(1, 1)
        || !fullRequestWriter.write(kWireMember + 1U, 6)
        || !fullRequestWriter.write(0, 1) || !fullRequestWriter.write(0, 1)
        || !fullRequestWriter.write(1, 1)
        || !fullRequestWriter.write(0, 1) || !fullRequestWriter.write(1, 1)
        || !fullRequestWriter.write(kWireMember + 1U, 6)) {
        std::fputs("full ID39 fixture failed to encode\n", stderr);
        return 5;
    }
    std::size_t fullRequestBytes = 0;
    constexpr std::array<std::byte, 19> kExpectedRequest{
        std::byte{0x88}, std::byte{0x77}, std::byte{0x66}, std::byte{0x55},
        std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11},
        std::byte{0x30}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x61}, std::byte{0x14}, std::byte{0x20}};
    if (!fullRequestWriter.finish(fullRequestBytes)
        || fullRequestWriter.bit_count() != 148 || fullRequestBytes != kExpectedRequest.size()
        || std::memcmp(fullRequestStorage.data(),
                       kExpectedRequest.data(),
                       kExpectedRequest.size())
               != 0) {
        std::fputs("ID39 request golden mismatch\n", stderr);
        for (std::size_t index = 0; index < fullRequestBytes; ++index) {
            std::fprintf(stderr,
                         "%02X",
                         std::to_integer<unsigned>(fullRequestStorage[index]));
        }
        std::fputc('\n', stderr);
        return 6;
    }
    bits::Reader fullRequestReader(
        std::span<const std::byte>(fullRequestStorage.data(), fullRequestBytes), 148);
    group::ParameterRequestHeader decodedRequest{};
    group::EncodedParameterBody decodedSelection{};
    group::EncodedParameterBody decodedResponses{};
    if (!group::read_parameter_request(fullRequestReader, decodedRequest)
        || decodedRequest.sessionId != kSessionId || decodedRequest.modeFlag
        || decodedRequest.requestedMask != kAuthorityMask
        || !group::read_activity_selection_proposal(fullRequestReader, decodedSelection)
        || !group::read_activity_selection_responses_proposal(fullRequestReader,
                                                              decodedResponses)
        || fullRequestReader.remaining_bits() != 0) {
        std::fputs("ID39 request did not consume its exact meaningful boundary\n", stderr);
        return 7;
    }

    std::array<std::byte, 8> reboundSelectionStorage{};
    std::array<std::byte, 8> reboundResponsesStorage{};
    bits::Writer reboundSelectionWriter(reboundSelectionStorage);
    bits::Writer reboundResponsesWriter(reboundResponsesStorage);
    std::size_t reboundSelectionBytes = 0;
    std::size_t reboundResponsesBytes = 0;
    if (!reboundSelectionWriter.write(1, 1)
        || !write_captured(reboundSelectionWriter, selection)
        || !reboundSelectionWriter.finish(reboundSelectionBytes)
        || !reboundResponsesWriter.write(1, 1)
        || !write_captured(reboundResponsesWriter, responses)
        || !reboundResponsesWriter.finish(reboundResponsesBytes)) {
        std::fputs("rebound proposal fixtures did not encode\n", stderr);
        return 8;
    }
    bits::Reader reboundSelection(
        std::span<const std::byte>(reboundSelectionStorage.data(), reboundSelectionBytes),
        1 + selection.bitCount);
    bits::Reader reboundResponses(
        std::span<const std::byte>(reboundResponsesStorage.data(), reboundResponsesBytes),
        1 + responses.bitCount);
    group::EncodedParameterBody checkedSelection{};
    group::EncodedParameterBody checkedResponses{};
    if (!group::read_activity_selection_proposal(reboundSelection, checkedSelection)
        || !group::read_activity_selection_responses_proposal(reboundResponses, checkedResponses)
        || checkedSelection.memberIndex != kAuthoritativeMember
        || checkedResponses.memberIndex != kAuthoritativeMember) {
        std::fputs("rebound proposal fields did not round-trip\n", stderr);
        return 9;
    }

    std::array<const group::EncodedParameterBody*, group::kParameterMemberCapacity> selections{};
    std::array<const group::EncodedParameterBody*, group::kParameterMemberCapacity> responseRows{};
    selections[kAuthoritativeMember] = &selection;
    responseRows[kAuthoritativeMember] = &responses;

    group::ParameterUpdate update{};
    update.sessionId = kSessionId;
    update.carriedMask = kAuthorityMask;
    update.activitySelections = selections;
    update.activitySelectionResponses = responseRows;

    std::array<std::byte, 64> updateStorage{};
    bits::Writer updateWriter(updateStorage);
    std::size_t updateBytes = 0;
    if (!group::write_parameter_update(updateWriter, update)
        || !updateWriter.finish(updateBytes) || updateWriter.bit_count() != 138
        || updateBytes != 18) {
        std::fprintf(stderr,
                     "authority update size mismatch: bits=%zu bytes=%zu\n",
                     updateWriter.bit_count(),
                     updateBytes);
        return 10;
    }

    constexpr std::array<std::byte, 18> kExpectedUpdate{
        std::byte{0x44}, std::byte{0x3B}, std::byte{0xB3}, std::byte{0x2A},
        std::byte{0xA2}, std::byte{0x19}, std::byte{0x91}, std::byte{0x08},
        std::byte{0x88}, std::byte{0x36}, std::byte{0x10}, std::byte{0x00},
        std::byte{0x62}, std::byte{0x10}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}};
    if (std::memcmp(updateStorage.data(), kExpectedUpdate.data(), kExpectedUpdate.size()) != 0) {
        std::fputs("ID38 authority-update golden mismatch\n", stderr);
        for (std::size_t index = 0; index < updateBytes; ++index) {
            std::fprintf(stderr,
                         "%02X",
                         std::to_integer<unsigned>(updateStorage[index]));
        }
        std::fputc('\n', stderr);
        return 11;
    }

    bits::Reader updateReader(std::span<const std::byte>(updateStorage.data(), updateBytes),
                              updateWriter.bit_count());
    std::uint64_t decodedSession = 0;
    bool ok = expect(updateReader, 1, 0, "reset")
              && bits::read_raw_u64(updateReader, decodedSession) && decodedSession == kSessionId
              && expect(updateReader, 2, 0, "released groups")
              && expect(updateReader, 2, 1, "carried groups");
    for (std::uint8_t parameter = 0; parameter < 5; ++parameter) {
        ok &= expect(updateReader, 1, 0, "pre-selection selector");
    }
    ok &= expect(updateReader, 1, 1, "parameter-5 selector");
    ok &= expect(updateReader, 1, 1, "parameter-5 outer root present");
    ok &= expect(updateReader, 1, 0, "parameter-5 member 0 absent");
    ok &= expect(updateReader, 1, 1, "parameter-5 member 1 present");
    ok &= expect(updateReader, 1, 1, "FC slot presence");
    ok &= expect(updateReader, 6, kAuthoritativeMember + 1U, "FC slot");
    ok &= expect(updateReader, 1, 0, "FC selection state absent");
    ok &= expect(updateReader, 1, 0, "FC launch state absent");
    for (std::uint8_t member = 2; member < group::kParameterMemberCapacity; ++member) {
        ok &= expect(updateReader, 1, 0, "parameter-5 remaining member absent");
    }
    ok &= expect(updateReader, 1, 1, "parameter-6 selector");
    ok &= expect(updateReader, 1, 1, "parameter-6 outer root present");
    ok &= expect(updateReader, 1, 0, "EC member 0 state absent");
    ok &= expect(updateReader, 1, 0, "EC member 0 slot absent");
    ok &= expect(updateReader, 1, 0, "EC member 1 state absent");
    ok &= expect(updateReader, 1, 1, "EC member 1 slot present");
    ok &= expect(updateReader, 6, kAuthoritativeMember + 1U, "EC member 1 slot");
    for (std::uint8_t member = 2; member < group::kParameterMemberCapacity; ++member) {
        ok &= expect(updateReader, 1, 0, "EC remaining state absent");
        ok &= expect(updateReader, 1, 0, "EC remaining slot absent");
    }
    for (std::uint8_t parameter = 7; parameter < 16; ++parameter) {
        ok &= expect(updateReader, 1, 0, "post-response selector");
    }
    ok &= updateReader.remaining_bits() == 0;
    if (!ok || update.releasedMask != 0) {
        std::fputs("authority update bit layout mismatch\n", stderr);
        return 12;
    }

    // PC 1.59 walks each ID39 parameter on its own: after the 0x60 snapshot the client requests
    // parameter 6 alone, so an EC-only request has no FC root and the answer only the E9 aggregate
    constexpr std::uint64_t kResponsesOnlyMask =
        std::uint64_t{1}
        << static_cast<std::uint8_t>(group::Parameter::activitySelectionResponses);
    std::array<std::byte, 24> responsesOnlyRequestStorage{};
    bits::Writer responsesOnlyRequestWriter(responsesOnlyRequestStorage);
    std::size_t responsesOnlyRequestBytes = 0;
    if (!bits::write_raw_u64(responsesOnlyRequestWriter, kSessionId)
        || !responsesOnlyRequestWriter.write(0, 1)
        || !bits::write_raw_u64(responsesOnlyRequestWriter, kResponsesOnlyMask)
        || !responsesOnlyRequestWriter.write(1, 1)
        || !responsesOnlyRequestWriter.write(0, 1)
        || !responsesOnlyRequestWriter.write(1, 1)
        || !responsesOnlyRequestWriter.write(kWireMember + 1U, 6)
        || !responsesOnlyRequestWriter.finish(responsesOnlyRequestBytes)
        || responsesOnlyRequestWriter.bit_count() != 138
        || responsesOnlyRequestBytes != 18) {
        std::fputs("EC-only ID39 request failed to encode exactly\n", stderr);
        return 32;
    }
    constexpr std::array<std::byte, 18> kExpectedResponsesOnlyRequest{
        std::byte{0x88}, std::byte{0x77}, std::byte{0x66}, std::byte{0x55},
        std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11},
        std::byte{0x20}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x50}, std::byte{0x80}};
    if (std::memcmp(responsesOnlyRequestStorage.data(),
                    kExpectedResponsesOnlyRequest.data(),
                    kExpectedResponsesOnlyRequest.size())
        != 0) {
        std::fputs("EC-only ID39 request golden mismatch\n", stderr);
        return 33;
    }

    bits::Reader responsesOnlyRequestReader(
        std::span<const std::byte>(responsesOnlyRequestStorage.data(),
                                  responsesOnlyRequestBytes),
        responsesOnlyRequestWriter.bit_count());
    group::ParameterRequestHeader responsesOnlyHeader{};
    group::EncodedParameterBody responsesOnlyProposal{};
    if (!group::read_parameter_request(responsesOnlyRequestReader, responsesOnlyHeader)
        || responsesOnlyHeader.sessionId != kSessionId || responsesOnlyHeader.modeFlag
        || responsesOnlyHeader.requestedMask != kResponsesOnlyMask
        || group::requests(
            responsesOnlyHeader.requestedMask,
            static_cast<std::uint8_t>(group::Parameter::activitySelection))
        || !group::requests(
            responsesOnlyHeader.requestedMask,
            static_cast<std::uint8_t>(group::Parameter::activitySelectionResponses))
        || !group::read_activity_selection_responses_proposal(
            responsesOnlyRequestReader, responsesOnlyProposal)
        || responsesOnlyRequestReader.remaining_bits() != 0
        || responsesOnlyProposal.bitCount != 8
        || !responsesOnlyProposal.memberIndexPresent
        || responsesOnlyProposal.memberIndex != kAuthoritativeMember) {
        std::fputs("EC-only ID39 request did not decode as one selected root\n", stderr);
        return 34;
    }

    std::array<const group::EncodedParameterBody*, group::kParameterMemberCapacity>
        responsesOnlyRows{};
    responsesOnlyRows[kAuthoritativeMember] = &responsesOnlyProposal;
    group::ParameterUpdate responsesOnlyUpdate{};
    responsesOnlyUpdate.sessionId = kSessionId;
    responsesOnlyUpdate.carriedMask = kResponsesOnlyMask;
    responsesOnlyUpdate.activitySelectionResponses = responsesOnlyRows;
    std::array<std::byte, 32> responsesOnlyUpdateStorage{};
    bits::Writer responsesOnlyUpdateWriter(responsesOnlyUpdateStorage);
    std::size_t responsesOnlyUpdateBytes = 0;
    if (!group::write_parameter_update(responsesOnlyUpdateWriter, responsesOnlyUpdate)
        || !responsesOnlyUpdateWriter.finish(responsesOnlyUpdateBytes)
        || responsesOnlyUpdateWriter.bit_count() != 116 || responsesOnlyUpdateBytes != 15
        || responsesOnlyUpdate.releasedMask != 0
        || responsesOnlyUpdate.carriedMask != kResponsesOnlyMask) {
        std::fputs("EC-only ID38 update failed to encode exactly\n", stderr);
        return 35;
    }
    constexpr std::array<std::byte, 15> kExpectedResponsesOnlyUpdate{
        std::byte{0x44}, std::byte{0x3B}, std::byte{0xB3}, std::byte{0x2A},
        std::byte{0xA2}, std::byte{0x19}, std::byte{0x91}, std::byte{0x08},
        std::byte{0x88}, std::byte{0x18}, std::byte{0x84}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    if (std::memcmp(responsesOnlyUpdateStorage.data(),
                    kExpectedResponsesOnlyUpdate.data(),
                    kExpectedResponsesOnlyUpdate.size())
        != 0) {
        std::fputs("EC-only ID38 update golden mismatch\n", stderr);
        return 36;
    }

    bits::Reader responsesOnlyUpdateReader(
        std::span<const std::byte>(responsesOnlyUpdateStorage.data(),
                                  responsesOnlyUpdateBytes),
        responsesOnlyUpdateWriter.bit_count());
    std::uint64_t responsesOnlySession = 0;
    bool responsesOnlyOk = expect(responsesOnlyUpdateReader, 1, 0, "EC-only reset")
                           && bits::read_raw_u64(responsesOnlyUpdateReader,
                                                 responsesOnlySession)
                           && responsesOnlySession == kSessionId
                           && expect(responsesOnlyUpdateReader,
                                     2,
                                     0,
                                     "EC-only released groups")
                           && expect(responsesOnlyUpdateReader,
                                     2,
                                     1,
                                     "EC-only carried groups");
    for (std::uint8_t parameter = 0; parameter < 6; ++parameter) {
        responsesOnlyOk &= expect(responsesOnlyUpdateReader,
                                  1,
                                  0,
                                  "EC-only pre-response selector");
    }
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 1, "parameter-6 selector");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 1, "parameter-6 outer root");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 0, "EC-only member 0 state");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 0, "EC-only member 0 slot");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 0, "EC-only member 1 state");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader, 1, 1, "EC-only member 1 slot");
    responsesOnlyOk &= expect(responsesOnlyUpdateReader,
                              6,
                              kAuthoritativeMember + 1U,
                              "EC-only member 1 index");
    for (std::uint8_t member = 2; member < group::kParameterMemberCapacity; ++member) {
        responsesOnlyOk &= expect(responsesOnlyUpdateReader,
                                  1,
                                  0,
                                  "EC-only remaining state");
        responsesOnlyOk &= expect(responsesOnlyUpdateReader,
                                  1,
                                  0,
                                  "EC-only remaining slot");
    }
    for (std::uint8_t parameter = 7; parameter < 16; ++parameter) {
        responsesOnlyOk &= expect(responsesOnlyUpdateReader,
                                  1,
                                  0,
                                  "EC-only post-response selector");
    }
    if (!responsesOnlyOk || responsesOnlyUpdateReader.remaining_bits() != 0) {
        std::fputs("EC-only ID38 update contained FC or trailing aggregate bits\n", stderr);
        return 37;
    }

    std::array<std::byte, 8> secondProposalStorage{};
    bits::Writer secondProposalWriter(secondProposalStorage);
    constexpr std::uint8_t kSecondMember = 2;
    if (!secondProposalWriter.write(1, 1)
        || !secondProposalWriter.write(1, 1)
        || !secondProposalWriter.write(kSecondMember + 1U, 6)
        || !secondProposalWriter.write(0, 1) || !secondProposalWriter.write(0, 1)
        || !secondProposalWriter.write(1, 1)
        || !secondProposalWriter.write(0, 1) || !secondProposalWriter.write(1, 1)
        || !secondProposalWriter.write(kSecondMember + 1U, 6)) {
        std::fputs("second-member proposal fixture failed to encode\n", stderr);
        return 13;
    }
    std::size_t secondProposalBytes = 0;
    if (!secondProposalWriter.finish(secondProposalBytes)
        || secondProposalWriter.bit_count() != 19) {
        std::fputs("second-member proposal fixture size mismatch\n", stderr);
        return 14;
    }
    bits::Reader secondProposalReader(
        std::span<const std::byte>(secondProposalStorage.data(), secondProposalBytes), 19);
    group::EncodedParameterBody secondSelection{};
    group::EncodedParameterBody secondResponses{};
    if (!group::read_activity_selection_proposal(secondProposalReader, secondSelection)
        || !group::read_activity_selection_responses_proposal(secondProposalReader,
                                                              secondResponses)
        || secondProposalReader.remaining_bits() != 0
        || !group::validate_parameter_proposal_member(secondSelection, kSecondMember)
        || !group::validate_parameter_proposal_member(secondResponses, kSecondMember)) {
        std::fputs("second-member proposal did not split or bind exactly\n", stderr);
        return 15;
    }
    selections[kSecondMember] = &secondSelection;
    responseRows[kSecondMember] = &secondResponses;
    group::ParameterUpdate twoMemberUpdate = update;
    twoMemberUpdate.activitySelections = selections;
    twoMemberUpdate.activitySelectionResponses = responseRows;
    std::array<std::byte, 64> twoMemberStorage{};
    bits::Writer twoMemberWriter(twoMemberStorage);
    std::size_t twoMemberBytes = 0;
    if (!group::write_parameter_update(twoMemberWriter, twoMemberUpdate)
        || !twoMemberWriter.finish(twoMemberBytes) || twoMemberWriter.bit_count() != 153
        || twoMemberBytes != 20 || twoMemberUpdate.releasedMask != 0
        || twoMemberUpdate.carriedMask != kAuthorityMask) {
        std::fputs("two-member authority aggregate was not 153 bits\n", stderr);
        return 16;
    }

    std::array<std::byte, 1024> fullSelectionStorage{};
    bits::Writer fullSelectionWriter(fullSelectionStorage);
    std::size_t fullSelectionBytes = 0;
    if (!write_full_selection_fixture(fullSelectionWriter,
                                      kAuthoritativeMember,
                                      16,
                                      32)
        || !fullSelectionWriter.finish(fullSelectionBytes)) {
        std::fputs("full FC boundary fixture failed to encode\n", stderr);
        return 17;
    }
    const std::size_t fullSelectionBits = fullSelectionWriter.bit_count();
    bits::Reader fullSelectionReader(
        std::span<const std::byte>(fullSelectionStorage.data(), fullSelectionBytes),
        fullSelectionBits);
    group::EncodedParameterBody fullSelection{};
    if (fullSelectionBits != 3453
        || !group::read_activity_selection_proposal(fullSelectionReader, fullSelection)
        || fullSelection.bitCount != fullSelectionBits - 1
        || fullSelectionReader.remaining_bits() != 0
        || !group::validate_parameter_proposal_member(fullSelection,
                                                      kAuthoritativeMember)) {
        std::fputs("full FC descriptor/count boundary was rejected\n", stderr);
        return 18;
    }
    bits::Reader oneBitShort(
        std::span<const std::byte>(fullSelectionStorage.data(), fullSelectionBytes),
        fullSelectionBits - 1);
    const std::size_t oneBitShortBefore = oneBitShort.remaining_bits();
    group::EncodedParameterBody rejected{};
    if (group::read_activity_selection_proposal(oneBitShort, rejected)
        || oneBitShort.remaining_bits() != oneBitShortBefore) {
        std::fputs("exact-bit FC truncation was not rejected atomically\n", stderr);
        return 19;
    }

    // the five-bit skull count caps at 16 and the six-bit pair count at 32
    std::array<std::byte, 1024> invalidBoundaryStorage{};
    bits::Writer invalidSkullsWriter(invalidBoundaryStorage);
    std::size_t invalidBoundaryBytes = 0;
    if (!write_full_selection_fixture(invalidSkullsWriter,
                                      kAuthoritativeMember,
                                      17,
                                      32)
        || !invalidSkullsWriter.finish(invalidBoundaryBytes)) {
        std::fputs("invalid skull-count fixture failed to encode\n", stderr);
        return 20;
    }
    bits::Reader invalidSkullsReader(
        std::span<const std::byte>(invalidBoundaryStorage.data(), invalidBoundaryBytes),
        invalidSkullsWriter.bit_count());
    const std::size_t invalidSkullsBefore = invalidSkullsReader.remaining_bits();
    if (group::read_activity_selection_proposal(invalidSkullsReader, rejected)
        || invalidSkullsReader.remaining_bits() != invalidSkullsBefore) {
        std::fputs("FC skull count 17 was not rejected atomically\n", stderr);
        return 21;
    }

    invalidBoundaryStorage = {};
    bits::Writer invalidPairsWriter(invalidBoundaryStorage);
    if (!write_full_selection_fixture(invalidPairsWriter,
                                      kAuthoritativeMember,
                                      16,
                                      33)
        || !invalidPairsWriter.finish(invalidBoundaryBytes)) {
        std::fputs("invalid pair-count fixture failed to encode\n", stderr);
        return 22;
    }
    bits::Reader invalidPairsReader(
        std::span<const std::byte>(invalidBoundaryStorage.data(), invalidBoundaryBytes),
        invalidPairsWriter.bit_count());
    const std::size_t invalidPairsBefore = invalidPairsReader.remaining_bits();
    if (group::read_activity_selection_proposal(invalidPairsReader, rejected)
        || invalidPairsReader.remaining_bits() != invalidPairsBefore) {
        std::fputs("FC pair count 33 was not rejected atomically\n", stderr);
        return 23;
    }

    const std::array<std::byte, 1> truncatedSelection{std::byte{0xC0}};
    bits::Reader truncatedSelectionReader(truncatedSelection);
    const std::size_t selectionBefore = truncatedSelectionReader.remaining_bits();
    if (group::read_activity_selection_proposal(truncatedSelectionReader, rejected)
        || truncatedSelectionReader.remaining_bits() != selectionBefore) {
        std::fputs("truncated FC root was not rejected atomically\n", stderr);
        return 24;
    }
    const std::array<std::byte, 2> outOfRangeSelection{
        std::byte{0xCE}, std::byte{0x00}};
    bits::Reader outOfRangeSelectionReader(outOfRangeSelection, 10);
    group::EncodedParameterBody outOfRange{};
    if (!group::read_activity_selection_proposal(outOfRangeSelectionReader, outOfRange)
        || outOfRange.memberIndex != 13
        || group::validate_parameter_proposal_member(outOfRange, kAuthoritativeMember)) {
        std::fputs("out-of-range FC scalar was not separated from semantic binding\n", stderr);
        return 25;
    }

    // native biased int8 zero decodes to the contextual -1 producer sentinel
    const std::array<std::byte, 2> sentinelSelection{
        std::byte{0xC0}, std::byte{0x00}};
    bits::Reader sentinelSelectionReader(sentinelSelection, 10);
    group::EncodedParameterBody sentinel{};
    if (!group::read_activity_selection_proposal(sentinelSelectionReader, sentinel)
        || !sentinel.present || !sentinel.memberIndexPresent
        || sentinel.memberIndex != group::kUnspecifiedParameterMember
        || !group::validate_parameter_proposal_member(sentinel, kAuthoritativeMember)) {
        std::fputs("FC native -1 producer sentinel was rejected\n", stderr);
        return 26;
    }

    // a sentinel request must swap its six zero bits for row+1, or it overwrites F8's row initializer with -1
    std::array<const group::EncodedParameterBody*, group::kParameterMemberCapacity>
        sentinelSelections{};
    sentinelSelections[kAuthoritativeMember] = &sentinel;
    group::ParameterUpdate sentinelUpdate{};
    sentinelUpdate.sessionId = kSessionId;
    sentinelUpdate.carriedMask =
        std::uint64_t{1} << static_cast<std::uint8_t>(group::Parameter::activitySelection);
    sentinelUpdate.activitySelections = sentinelSelections;
    std::array<std::byte, 32> sentinelUpdateStorage{};
    bits::Writer sentinelUpdateWriter(sentinelUpdateStorage);
    std::size_t sentinelUpdateBytes = 0;
    if (!group::write_parameter_update(sentinelUpdateWriter, sentinelUpdate)
        || !sentinelUpdateWriter.finish(sentinelUpdateBytes)) {
        std::fputs("FC sentinel authority update failed to encode\n", stderr);
        return 27;
    }
    bits::Reader sentinelUpdateReader(
        std::span<const std::byte>(sentinelUpdateStorage.data(), sentinelUpdateBytes),
        sentinelUpdateWriter.bit_count());
    std::uint64_t ignoredSession = 0;
    if (!expect(sentinelUpdateReader, 1, 0, "sentinel reset")
        || !bits::read_raw_u64(sentinelUpdateReader, ignoredSession)
        || ignoredSession != kSessionId
        || !expect(sentinelUpdateReader, 2, 0, "sentinel released groups")
        || !expect(sentinelUpdateReader, 2, 1, "sentinel carried groups")) {
        std::fputs("FC sentinel authority header mismatch\n", stderr);
        return 28;
    }
    for (std::uint8_t parameter = 0; parameter < 5; ++parameter) {
        if (!expect(sentinelUpdateReader, 1, 0, "sentinel pre-selection selector")) {
            return 29;
        }
    }
    if (!expect(sentinelUpdateReader, 1, 1, "sentinel parameter-5 selector")
        || !expect(sentinelUpdateReader, 1, 1, "sentinel parameter-5 root")
        || !expect(sentinelUpdateReader, 1, 0, "sentinel member 0 absent")
        || !expect(sentinelUpdateReader, 1, 1, "sentinel member 1 present")
        || !expect(sentinelUpdateReader, 1, 1, "sentinel member scalar present")
        || !expect(sentinelUpdateReader,
                   6,
                   kAuthoritativeMember + 1U,
                   "sentinel canonical member scalar")) {
        std::fputs("FC sentinel was not canonicalized to its authority row\n", stderr);
        return 30;
    }

    const std::array<std::byte, 1> emptyRoots{std::byte{0x00}};
    bits::Reader emptyRootsReader(emptyRoots, 2);
    group::EncodedParameterBody emptySelection{};
    group::EncodedParameterBody emptyResponses{};
    if (!group::read_activity_selection_proposal(emptyRootsReader, emptySelection)
        || !group::read_activity_selection_responses_proposal(emptyRootsReader, emptyResponses)
        || emptyRootsReader.remaining_bits() != 0 || emptySelection.present
        || emptyResponses.present
        || !group::validate_parameter_proposal_member(emptySelection, kAuthoritativeMember)
        || !group::validate_parameter_proposal_member(emptyResponses, kAuthoritativeMember)) {
        std::fputs("clear contextual proposal roots were rejected\n", stderr);
        return 31;
    }

    std::printf("PASS: typed parameter-5/6 authority snapshot is exact (%zu bits)\n",
                updateWriter.bit_count());
    return 0;
}
