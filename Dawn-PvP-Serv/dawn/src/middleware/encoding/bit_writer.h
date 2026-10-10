#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::encoding::bits {

class Writer final {
public:
    explicit Writer(std::span<std::byte> output) noexcept;

    // counts bits only and refuses no width, so an encoder can size its body before writing
    [[nodiscard]] static Writer measuring() noexcept;

    [[nodiscard]] bool write(std::uint64_t value, std::uint8_t width) noexcept;

    [[nodiscard]] bool finish(std::size_t& written) const noexcept;

    [[nodiscard]] std::size_t bit_count() const noexcept;

private:
    std::span<std::byte> output_;
    std::size_t bitPosition_{};
    bool failed_{};
    bool measuring_{};
};

} // namespace dawn::middleware::encoding::bits
