#include <array>
#include <cstddef>
#include <cstdint>
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

} // namespace

int main() {
    constexpr std::uint64_t kSessionId = 0x1122334455667788ULL;
    constexpr std::uint64_t kGoalBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(group::Parameter::worldControllerGoalData);
    constexpr std::uint64_t kCurrentActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(group::Parameter::currentActivity);
    constexpr std::uint64_t kPreviousActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(group::Parameter::previousActivity);

    group::ParameterUpdate update{};
    update.sessionId = kSessionId;
    update.carriedMask = kGoalBit | kCurrentActivityBit;
    update.currentActivity.present = true;

    std::array<std::byte, 64> storage{};
    bits::Writer writer(storage);
    std::size_t encodedBytes = 0;
    if (!group::write_parameter_update(writer, update) || !writer.finish(encodedBytes)) {
        std::fputs("orbit parameter update failed to encode\n", stderr);
        return 1;
    }

    constexpr std::size_t kExpectedBits = 161;
    if (writer.bit_count() != kExpectedBits || encodedBytes != 21) {
        std::fprintf(stderr,
                     "encoded size mismatch: bytes=%zu bits=%zu\n",
                     encodedBytes,
                     writer.bit_count());
        return 2;
    }

    bits::Reader reader(std::span<const std::byte>(storage.data(), encodedBytes));
    std::uint64_t rawSessionId = 0;
    bool ok = expect(reader, 1, 0, "reset") && bits::read_raw_u64(reader, rawSessionId)
              && rawSessionId == kSessionId && expect(reader, 2, 0, "released groups")
              && expect(reader, 2, 1, "carried groups");

    ok &= expect(reader, 1, 1, "goal selector");
    ok &= expect(reader, 6, 30, "goal step + 1");
    ok &= expect(reader, 9, 1, "goal reason + 1");
    ok &= expect(reader, 8, 1, "goal accepted sequence");

    for (std::uint8_t parameter = 1; parameter < 7; ++parameter) {
        ok &= expect(reader, 1, 0, "pre-current selector");
    }

    ok &= expect(reader, 1, 1, "current-activity selector");
    ok &= expect(reader, 1, 1, "current-activity root");
    ok &= expect(reader, 4, 1, "current reason + 1");
    ok &= expect(reader, 12, 1, "current source index + 1");
    ok &= expect(reader, 12, 1, "current activity index + 1");
    ok &= expect(reader, 1, 0, "element index presence");
    ok &= expect(reader, 1, 0, "account id presence");
    ok &= expect(reader, 1, 0, "nonce presence");
    ok &= expect(reader, 5, 0, "skull count");
    ok &= expect(reader, 8, 2, "fixed current-activity byte");
    for (std::uint8_t field = 0; field < 5; ++field) {
        ok &= expect(reader, 1, 0, "optional current-activity field");
    }
    ok &= expect(reader, 1, 0, "mandatory current-activity boolean");
    ok &= expect(reader, 1, 0, "first trailing array presence");
    ok &= expect(reader, 1, 0, "second trailing array presence");

    // parameters 8..15 are absent; seven zero padding bits complete byte 21
    for (std::uint8_t parameter = 8; parameter < 16; ++parameter) {
        ok &= expect(reader, 1, 0, "post-current selector");
    }
    ok &= reader.remaining_bits() == 7;
    ok &= (group::kEncodableParameters & update.carriedMask) == update.carriedMask;

    // public Citizen joins keep the parameter-zero release latch and carry the destination's
    // concrete activity; previous actual-activity-index starts at zero (EDZ is activity 8 here)
    group::ParameterUpdate publicUpdate{};
    publicUpdate.sessionId = kSessionId;
    publicUpdate.releasedMask = kGoalBit;
    publicUpdate.carriedMask = kCurrentActivityBit | kPreviousActivityBit;
    publicUpdate.currentActivity.present = true;
    publicUpdate.currentActivity.reason = 0;
    publicUpdate.currentActivity.sourceActivityIndex = 8;
    publicUpdate.currentActivity.activityIndex = 8;
    publicUpdate.previousActivity.present = true;
    std::array<std::byte, 32> publicStorage{};
    bits::Writer publicWriter(publicStorage);
    std::size_t publicBytes = 0;
    if (!group::write_parameter_update(publicWriter, publicUpdate)
        || !publicWriter.finish(publicBytes)) {
        std::fputs("public Citizen parameter latch failed to encode\n", stderr);
        return 3;
    }
    if (publicWriter.bit_count() != 207 || publicBytes != 26) {
        std::fprintf(stderr,
                     "public latch size mismatch: bytes=%zu bits=%zu\n",
                     publicBytes,
                     publicWriter.bit_count());
        return 4;
    }

    bits::Reader publicReader(
        std::span<const std::byte>(publicStorage.data(), publicBytes));
    rawSessionId = 0;
    ok &= expect(publicReader, 1, 0, "public reset")
          && bits::read_raw_u64(publicReader, rawSessionId) && rawSessionId == kSessionId
          && expect(publicReader, 2, 1, "public released groups")
          && expect(publicReader, 2, 1, "public carried groups")
          && expect(publicReader, 1, 1, "public goal release");
    for (std::uint8_t parameter = 1; parameter < 16; ++parameter) {
        ok &= expect(publicReader, 1, 0, "public non-goal release");
    }
    for (std::uint8_t parameter = 0; parameter < 7; ++parameter) {
        ok &= expect(publicReader, 1, 0, "public pre-current selector");
    }
    ok &= expect(publicReader, 1, 1, "public current selector");
    ok &= expect(publicReader, 1, 1, "public current root");
    ok &= expect(publicReader, 4, 1, "public current reason + 1");
    ok &= expect(publicReader, 12, 9, "public current source + 1");
    ok &= expect(publicReader, 12, 9, "public current activity + 1");
    ok &= expect(publicReader, 1, 0, "public element index presence");
    ok &= expect(publicReader, 1, 0, "public account id presence");
    ok &= expect(publicReader, 1, 0, "public nonce presence");
    ok &= expect(publicReader, 5, 0, "public skull count");
    ok &= expect(publicReader, 8, 2, "public fixed current-activity byte");
    for (std::uint8_t field = 0; field < 8; ++field) {
        ok &= expect(publicReader, 1, 0, "public current tail");
    }
    ok &= expect(publicReader, 1, 1, "public previous selector");
    ok &= expect(publicReader, 1, 1, "public previous root");
    ok &= expect(publicReader, 4, 1, "public previous reason + 1");
    ok &= expect(publicReader, 12, 1, "public previous source + 1");
    ok &= expect(publicReader, 12, 1, "public previous activity + 1");
    ok &= expect(publicReader, 1, 0, "public previous element index presence");
    ok &= expect(publicReader, 1, 0, "public previous account id presence");
    ok &= expect(publicReader, 1, 0, "public previous nonce presence");
    ok &= expect(publicReader, 5, 0, "public previous skull count");
    ok &= expect(publicReader, 8, 2, "public fixed previous-activity byte");
    for (std::uint8_t field = 0; field < 8; ++field) {
        ok &= expect(publicReader, 1, 0, "public previous tail");
    }
    for (std::uint8_t parameter = 9; parameter < 16; ++parameter) {
        ok &= expect(publicReader, 1, 0, "public post-previous selector");
    }
    ok &= publicReader.remaining_bits() == 1;

    if (!ok) {
        return 5;
    }
    std::printf(
        "PASS: central Orbit and public Citizen EDZ current/previous activity values encode (%zu/%zu bits)\n",
        writer.bit_count(),
        publicWriter.bit_count());
    return 0;
}
