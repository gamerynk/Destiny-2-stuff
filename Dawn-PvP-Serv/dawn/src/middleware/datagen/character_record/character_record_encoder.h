#pragma once

#include <cstddef>
#include <span>

#include "../../../state/account/account_state.h"
#include "../family4/loadout/definition.h"
#include "layout.h"

namespace dawn::middleware::datagen::character_record {

// family-three record has an extra reserved block before the appearance
inline constexpr std::size_t kFamily3RecordSize = 3'904;
inline constexpr std::size_t kFamily0RecordSize = 3'856;
inline constexpr std::size_t kFamily0AnchorSize = 96;

[[nodiscard]] bool encode_family3(const state::CharacterState& character,
                                  const family4::loadout::ResolvedInstances& instances,
                                  std::int32_t light,
                                  std::span<std::byte> output) noexcept;

[[nodiscard]] bool encode_family0(const state::CharacterState& character,
                                  const family4::loadout::ResolvedInstances& instances,
                                  std::int32_t light,
                                  std::span<std::byte> output) noexcept;

// anchor must name the same character as the record beside it or the client rejects both
[[nodiscard]] bool encode_family0_anchor(std::uint64_t accountSoid,
                                         std::uint64_t characterSoid,
                                         std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::datagen::character_record
