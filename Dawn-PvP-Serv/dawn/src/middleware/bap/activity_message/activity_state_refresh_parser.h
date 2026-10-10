#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::state_refresh {

inline constexpr std::uint32_t kMessageType = 18;
inline constexpr std::size_t kEncodedSize = 8;

struct StateRefresh final {
    std::uint32_t membershipRevision{};
    std::int32_t bubbleIndex{};
};

[[nodiscard]] bool parse_state_refresh(std::span<const std::byte> input,
                                       StateRefresh& refresh) noexcept;

} // namespace dawn::middleware::bap::activity_message::state_refresh
