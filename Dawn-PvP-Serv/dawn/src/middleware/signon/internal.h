#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::middleware::signon {

inline constexpr unsigned kWireVarint = 0;
inline constexpr unsigned kWireBytes = 2;
inline constexpr unsigned kWireTypeBits = 3;
inline constexpr std::uint64_t kVarintPayloadMask = 0x7F;
inline constexpr unsigned kVarintPayloadBits = 7;
inline constexpr unsigned char kVarintContinuation = 0x80;

[[nodiscard]] inline std::span<const std::byte> text_bytes(std::string_view text) noexcept {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

class Writer {
public:
    explicit Writer(std::span<std::byte> output) noexcept : output_(output) {}

    [[nodiscard]] bool varint(unsigned field, std::uint64_t value) noexcept {
        return raw_varint((static_cast<std::uint64_t>(field) << kWireTypeBits) | kWireVarint)
               && raw_varint(value);
    }

    [[nodiscard]] bool bytes(unsigned field, std::span<const std::byte> value) noexcept {
        if (!raw_varint((static_cast<std::uint64_t>(field) << kWireTypeBits) | kWireBytes)
            || !raw_varint(value.size()) || value.size() > output_.size() - size_) {
            return false;
        }
        for (const std::byte byte : value) {
            output_[size_++] = byte;
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

private:
    [[nodiscard]] bool raw_varint(std::uint64_t value) noexcept {
        do {
            if (size_ == output_.size()) {
                return false;
            }
            unsigned char byte = static_cast<unsigned char>(value & kVarintPayloadMask);
            value >>= kVarintPayloadBits;
            if (value != 0) {
                byte |= kVarintContinuation;
            }
            output_[size_++] = static_cast<std::byte>(byte);
        } while (value != 0);
        return true;
    }

    std::span<std::byte> output_;
    std::size_t size_{};
};

} // namespace dawn::middleware::signon
