#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::protobuf {

enum class WireType : std::uint8_t {
    varint = 0,
    fixed64 = 1,
    lengthDelimited = 2,
    fixed32 = 5,
};

struct Field final {
    std::uint32_t fieldNumber{};
    WireType wireType{WireType::varint};
    std::uint64_t value{};
    std::span<const std::byte> bytes{};
};

[[nodiscard]] bool
measure_varint_field(std::uint32_t fieldNumber, std::uint64_t value, std::size_t& size) noexcept;

[[nodiscard]] bool measure_length_delimited_field(std::uint32_t fieldNumber,
                                                  std::size_t payloadSize,
                                                  std::size_t& size) noexcept;

class Reader final {
public:
    explicit Reader(std::span<const std::byte> input) noexcept;

    [[nodiscard]] bool next(Field& field) noexcept;

    [[nodiscard]] std::size_t remaining() const noexcept;

private:
    std::span<const std::byte> input_;
    std::size_t cursor_{};
};

class Writer final {
public:
    explicit Writer(std::span<std::byte> output) noexcept;

    [[nodiscard]] bool write_varint(std::uint32_t fieldNumber, std::uint64_t value) noexcept;

    [[nodiscard]] bool write_length_delimited(std::uint32_t fieldNumber,
                                              std::span<const std::byte> value) noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::span<std::byte> output_;
    std::size_t size_{};
};

} // namespace dawn::middleware::protobuf
