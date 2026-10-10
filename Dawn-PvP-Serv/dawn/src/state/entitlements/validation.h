#pragma once

#include <cstdint>

#include "definition.h"

namespace dawn::state::entitlements {

[[nodiscard]] bool valid(const Entitlement& candidate) noexcept;

[[nodiscard]] bool valid(const Table& candidate) noexcept;

[[nodiscard]] bool
owned_identifier(const Table& table, std::size_t index, std::uint32_t& identifier) noexcept;

[[nodiscard]] Table authored() noexcept;

} // namespace dawn::state::entitlements
