#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::membership_acknowledgement {

inline constexpr std::uint32_t kMessageType = 38;
inline constexpr std::size_t kEncodedSize = 4;

struct MembershipAcknowledgement final {
    std::uint32_t membershipRevision{};
};

[[nodiscard]] bool
parse_membership_acknowledgement(std::span<const std::byte> input,
                                 MembershipAcknowledgement& acknowledgement) noexcept;

} // namespace dawn::middleware::bap::activity_message::membership_acknowledgement
