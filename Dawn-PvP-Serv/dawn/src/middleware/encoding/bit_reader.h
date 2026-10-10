#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::encoding::bits {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> input) noexcept;

    Reader(std::span<const std::byte> input, std::size_t bitCount) noexcept;

    [[nodiscard]] bool read(std::uint8_t width, std::uint64_t& value) noexcept;

    // a failed bound check marks the reader failed and leaves the cursor alone
    [[nodiscard]] bool skip(std::size_t width) noexcept;

    [[nodiscard]] std::size_t remaining_bits() const noexcept;

private:
    [[nodiscard]] bool can_consume(std::size_t width) noexcept;

    std::span<const std::byte> input_;
    std::size_t bitCapacity_{};
    std::size_t bitPosition_{};
    bool failed_{};
};

} // namespace dawn::middleware::encoding::bits
