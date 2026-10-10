#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace dawn::middleware::gameplay::group {

inline constexpr std::uint8_t kViewMessageId = 40;
inline constexpr std::uint32_t kViewMessageSize = 40;
inline constexpr std::size_t kViewListCapacity = 16;
inline constexpr std::uint8_t kMaximumViewKind = 5;

// both sides must agree on it; a mismatch produces no replicated entities at all
struct ViewEstablishment {
    std::array<std::byte, kViewListCapacity> list{};
    std::uint64_t sessionToken{};
    std::int32_t optionalValue{};
    std::uint8_t kind{};
    std::uint8_t listCount{};
    bool hasOptionalValue{};
    bool hasList{};
};

[[nodiscard]] bool read_view(encoding::bits::Reader& reader, ViewEstablishment& output) noexcept;

[[nodiscard]] bool write_view(encoding::bits::Writer& writer,
                              const ViewEstablishment& body) noexcept;

[[nodiscard]] bool compatible(const ViewEstablishment& left,
                              const ViewEstablishment& right) noexcept;

} // namespace dawn::middleware::gameplay::group
