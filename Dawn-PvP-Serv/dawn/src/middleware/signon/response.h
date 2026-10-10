#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../state/entitlements/definition.h"
#include "../../state/runtime/state.h"

namespace dawn::middleware::signon {
struct ContentConfiguration final {
    std::string_view url;
    std::string_view guid;
};

[[nodiscard]] bool encode_success(const state::SignOnState& state,
                                  const state::entitlements::Table& entitlements,
                                  std::uint64_t expirySeconds,
                                  std::uint64_t serverTimeSeconds,
                                  std::span<std::byte> output,
                                  std::size_t& written,
                                  ContentConfiguration content = {}) noexcept;

} // namespace dawn::middleware::signon
