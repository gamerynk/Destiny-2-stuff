#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../runtime/state.h"

namespace dawn::state::session {

struct Credentials final {
    SignOnState signOn{};
    BapState bap{};
    std::uint64_t accountSoid{};
    std::uint64_t platformId{};
    std::uint64_t generation{};
    std::uint64_t issuedAt{};
    std::uint64_t expiresAt{};
};

[[nodiscard]] bool issue(std::uint64_t accountSoid,
                         std::uint64_t platformId,
                         const SignOnState& bootstrapState,
                         std::uint64_t nowSeconds,
                         Credentials& credentials) noexcept;

// constant-time token compare
[[nodiscard]] bool authenticate(std::span<const std::byte, kSessionTokenSize> token,
                                std::uint64_t nowSeconds,
                                Credentials& credentials) noexcept;

[[nodiscard]] bool active(const Credentials& credentials,
                          std::uint64_t nowSeconds) noexcept;

// the callback runs with session issuance excluded and must not call session APIs; a
// failed callback stays retryable, and repeated hellos keep current picks
using GenerationInitializer = bool (*)(void*, std::uint64_t accountSoid) noexcept;
[[nodiscard]] bool initialize_game_generation(const Credentials& credentials,
                                              std::uint64_t nowSeconds,
                                              GenerationInitializer initializer,
                                              void* context) noexcept;

void shutdown() noexcept;

} // namespace dawn::state::session
