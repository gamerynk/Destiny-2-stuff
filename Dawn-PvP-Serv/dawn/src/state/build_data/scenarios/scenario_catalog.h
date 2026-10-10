#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "definition.h"

namespace dawn::state::build_data::scenarios {

void clear() noexcept;

// a destination names its groups by index, so a group table short by one row silently repoints every destination above it
[[nodiscard]] bool valid(std::span<const Definition> definitions,
                         std::span<const RosterGroup> groups) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions,
                           std::span<const RosterGroup> groups) noexcept;

[[nodiscard]] bool group(std::size_t index, RosterGroup& group) noexcept;

[[nodiscard]] std::size_t group_count() noexcept;

// one key can carry different layouts in different activities, so pass the tag when known
[[nodiscard]] bool group_by_key(std::uint32_t key, std::uint32_t objectTag, RosterGroup& group) noexcept;

[[nodiscard]] bool snapshot_groups(std::span<RosterGroup> output, std::size_t& count) noexcept;

[[nodiscard]] bool find(std::string_view name, Definition& definition) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::scenarios
