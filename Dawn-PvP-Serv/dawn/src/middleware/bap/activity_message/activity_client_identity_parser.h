#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::client_identity {

struct ClientIdentity final {
    std::uint64_t memberKey{};
    std::int32_t field1{};
    std::int32_t field2{};
    std::uint64_t field3{};
    std::uint64_t accountSoid{};
    std::uint64_t field5{};
    std::uint64_t field6{};
};

inline constexpr std::uint32_t kMessageType = 23;
inline constexpr std::size_t kMeaningfulBitCount = 362;
inline constexpr std::size_t kEncodedSize = 46;

[[nodiscard]] bool parse_client_identity(std::span<const std::byte> input,
                                         ClientIdentity& identity) noexcept;

} // namespace dawn::middleware::bap::activity_message::client_identity
