#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "definition.h"

namespace dawn::state::build_data::spawn_sets {

void clear() noexcept;

// a stem names its hashes by range, so a bank short by one row silently repoints every stem above it
[[nodiscard]] bool valid(std::span<const Stem> stems,
                         std::span<const NameHash> nameHashes) noexcept;

[[nodiscard]] bool valid_points(std::span<const Point> points,
                                std::span<const Stem> stems) noexcept;

[[nodiscard]] bool replace(std::span<const Stem> stems,
                           std::span<const NameHash> nameHashes,
                           std::span<const Point> points) noexcept;

[[nodiscard]] bool nearest_point(std::string_view stem,
                                 const std::array<float, kPositionComponents>& position,
                                 Point& point,
                                 float& distance) noexcept;

[[nodiscard]] bool snapshot_points(std::span<Point> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t point_count() noexcept;

[[nodiscard]] bool find(std::string_view name, Stem& stem) noexcept;

[[nodiscard]] bool
find_hash(std::string_view stem, std::uint32_t value, NameHash& nameHash) noexcept;

[[nodiscard]] bool
stem_hashes(const Stem& stem, std::span<NameHash> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot(std::span<Stem> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot_hashes(std::span<NameHash> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

[[nodiscard]] std::size_t hash_count() noexcept;

} // namespace dawn::state::build_data::spawn_sets
