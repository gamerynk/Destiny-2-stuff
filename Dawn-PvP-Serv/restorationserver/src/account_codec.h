#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "state/account/account_state.h"

namespace restoration::account_codec {

inline constexpr int kVersion = 7;
inline constexpr int kPreSetupVersion = 6;
inline constexpr int kPreFovVersion = 5;
inline constexpr int kPreInputVersion = 4;
inline constexpr int kVaultlessVersion = 3;
// v2 was written by the 2026-09-11 deploys (raw 36-byte header, no flag) and briefly by the
// flagged layout; try raw first, then flagged
inline constexpr int kLegacyHeaderVersion = 2;
inline constexpr int kPriorVersion = 1;

// kPriorVersion is what clients decode on the wire; their bundled codec is v1 and a v3
// payload broke every remote profile (missing emblems, greyed Join)
[[nodiscard]] bool encode(const dawn::state::AccountState& account,
                          std::vector<std::byte>& output,
                          int version = kVersion) noexcept;

[[nodiscard]] bool decode(std::span<const std::byte> input,
                          dawn::state::AccountState& account,
                          int version = kVersion) noexcept;

[[nodiscard]] bool self_test(const dawn::state::AccountState& account) noexcept;

} // namespace restoration::account_codec
