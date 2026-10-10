#include "bit_reader.h"

#include <limits>

#include "byte_order.h"

namespace dawn::middleware::encoding::bits {
namespace {

constexpr std::uint8_t kMaximumReadWidth = 64;

} // namespace

Reader::Reader(std::span<const std::byte> input) noexcept : input_(input) {
    const std::size_t maximumByteCount =
        (std::numeric_limits<std::size_t>::max)() / encoding::kBitsPerByte;
    if (input_.size() > maximumByteCount) {
        failed_ = true;
        return;
    }
    bitCapacity_ = input_.size() * encoding::kBitsPerByte;
}

Reader::Reader(std::span<const std::byte> input, std::size_t bitCount) noexcept
    : input_(input), bitCapacity_(bitCount) {
    const std::size_t maximumByteCount =
        (std::numeric_limits<std::size_t>::max)() / encoding::kBitsPerByte;
    if (input_.size() > maximumByteCount
        || bitCapacity_ > input_.size() * encoding::kBitsPerByte) {
        bitCapacity_ = 0;
        failed_ = true;
    }
}

bool Reader::can_consume(std::size_t width) noexcept {
    // multiply bytes to bits before forming the bit size, and check the cursor before subtracting
    if (failed_) {
        failed_ = true;
        return false;
    }
    if (bitPosition_ > bitCapacity_ || width > bitCapacity_ - bitPosition_) {
        failed_ = true;
        return false;
    }
    return true;
}

bool Reader::read(std::uint8_t width, std::uint64_t& value) noexcept {
    value = 0;
    if (width > kMaximumReadWidth) {
        failed_ = true;
        return false;
    }
    if (!can_consume(width)) {
        return false;
    }
    for (std::uint8_t index = 0; index < width; ++index) {
        const std::size_t byteIndex = bitPosition_ / encoding::kBitsPerByte;
        const std::size_t sourceShift =
            encoding::kBitsPerByte - 1 - (bitPosition_ % encoding::kBitsPerByte);
        const std::uint64_t bit =
            (std::to_integer<std::uint64_t>(input_[byteIndex]) >> sourceShift) & 1U;
        value = (value << 1U) | bit;
        ++bitPosition_;
    }
    return true;
}

bool Reader::skip(std::size_t width) noexcept {
    if (!can_consume(width)) {
        return false;
    }
    bitPosition_ += width;
    return true;
}

std::size_t Reader::remaining_bits() const noexcept {
    if (bitPosition_ > bitCapacity_) {
        return 0;
    }
    return bitCapacity_ - bitPosition_;
}

} // namespace dawn::middleware::encoding::bits
