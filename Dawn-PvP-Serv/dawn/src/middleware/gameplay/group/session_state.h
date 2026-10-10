#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "session_messages.h"

namespace dawn::middleware::gameplay::group {

inline constexpr std::size_t kSessionStateSize = 28768;

using SessionState = std::array<std::byte, kSessionStateSize>;

void build_session_state(const MembershipUpdate& body, SessionState& output) noexcept;

[[nodiscard]] std::uint32_t session_state_hash(const MembershipUpdate& body) noexcept;

} // namespace dawn::middleware::gameplay::group
