#include "activity_join_result_encoder.h"

#include <algorithm>

#include "../../encoding/byte_order.h"
#include "definition.h"

namespace dawn::middleware::bap::activity_message::join_result {
namespace {

struct JoinResultLayout final {
    static constexpr std::size_t correlation = 0;
    static constexpr std::size_t sessionId = correlation + encoding::kU32Size;
};

constexpr std::size_t kMeaningfulBitCount = 6'053;
constexpr std::size_t kJoinStatusBitOffset = 570;
constexpr std::size_t kJoinStatusBitCount = 3;
constexpr std::uint8_t kAcceptedJoinStatus = 1;
constexpr std::size_t kKeepaliveBitOffset = 1'781;
constexpr std::size_t kKeepaliveBitCount = encoding::kU16Size * encoding::kBitsPerByte;

static_assert(kEncodedSize
              == (kMeaningfulBitCount + encoding::kBitsPerByte - 1) / encoding::kBitsPerByte);
static_assert(kJoinStatusBitOffset + kJoinStatusBitCount <= kMeaningfulBitCount);
static_assert(kKeepaliveBitOffset + kKeepaliveBitCount <= kMeaningfulBitCount);

void write_bits(std::span<std::byte> output,
                std::size_t bitOffset,
                std::size_t bitCount,
                std::uint16_t value) noexcept {
    for (std::size_t index = 0; index < bitCount; ++index) {
        const std::size_t sourceShift = bitCount - index - 1;
        const auto sourceMask = static_cast<std::uint16_t>(1U << sourceShift);
        if ((value & sourceMask) == 0) {
            continue;
        }

        const std::size_t destinationBit = bitOffset + index;
        const std::size_t destinationByte = destinationBit / encoding::kBitsPerByte;
        const std::size_t destinationShift =
            encoding::kBitsPerByte - (destinationBit % encoding::kBitsPerByte) - 1;
        output[destinationByte] |= static_cast<std::byte>(1U << destinationShift);
    }
}

} // namespace

bool encode_join_result(std::uint32_t correlation,
                        std::uint64_t sessionId,
                        std::uint16_t keepaliveHintMs,
                        std::span<std::byte> output,
                        std::size_t& written,
                        std::uint8_t entityEpoch) noexcept {
    written = {};
    if (sessionId == kAbsentSessionId || output.size() < kEncodedSize) {
        return false;
    }

    const std::span body = output.first(kEncodedSize);
    std::fill(body.begin(), body.end(), std::byte{});
    encoding::write_u32_be(body.subspan<JoinResultLayout::correlation, encoding::kU32Size>(),
                           correlation);
    encoding::write_u64_be(body.subspan<JoinResultLayout::sessionId, encoding::kU64Size>(),
                           sessionId);
    write_bits(body, kJoinStatusBitOffset, kJoinStatusBitCount, kAcceptedJoinStatus);
    write_bits(body, kKeepaliveBitOffset, kKeepaliveBitCount, keepaliveHintMs);
    // native queue28 copies decoded +2A08 to roster+111E8, then view manager+D024;
    // this is schema field7, not the separate sixteen-byte patch epoch
    write_bits(body, 1'757, 8, entityEpoch);
    written = kEncodedSize;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::join_result
