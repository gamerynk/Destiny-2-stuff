#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::items::socket_plugs {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Rule> rules,
                         std::span<const Pool> pools,
                         std::span<const Member> members) noexcept;

[[nodiscard]] bool replace(std::span<const Rule> rules,
                           std::span<const Pool> pools,
                           std::span<const Member> members) noexcept;

[[nodiscard]] bool allowed(std::uint16_t itemDefinitionIndex,
                           std::uint8_t lane,
                           std::uint16_t plugDefinitionIndex) noexcept;

// the flat pool bank is small enough that this boot/acquisition query stays bounded
[[nodiscard]] bool contains(Member plugDefinitionIndex) noexcept;

[[nodiscard]] bool snapshot(std::span<Rule> rules,
                            std::size_t& ruleCount,
                            std::span<Pool> pools,
                            std::size_t& poolCount,
                            std::span<Member> members,
                            std::size_t& memberCount) noexcept;

[[nodiscard]] std::size_t rule_count() noexcept;

} // namespace dawn::state::build_data::items::socket_plugs
