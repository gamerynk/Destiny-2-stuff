#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../../state/build_data/hash_names/definition.h"

namespace dawn::client::content::hash_names {

namespace names_state = state::build_data::hash_names;

inline constexpr std::size_t kTagCapacity = 262'144;
inline constexpr std::size_t kBatchTags = 2048;
inline constexpr std::size_t kRunCapacity = names_state::kNameLength;
inline constexpr std::size_t kTokenCapacity = 12;

struct MatchState {
    std::span<const std::uint32_t> targets;
    std::span<names_state::Name> resolved;
};

struct Storage {
    std::array<std::uint32_t, names_state::kNameCapacity> targets{};
    std::size_t targetCount{};
    std::array<names_state::Name, names_state::kNameCapacity> resolved{};
    std::size_t resolvedCount{};
    std::array<std::uint32_t, kTagCapacity> tags{};
    std::size_t tagCount{};
    std::size_t cursor{};
    std::uint64_t startedTick{};
    bool collected{};
    bool invalid{};
};

void reset(Storage& storage) noexcept;

[[nodiscard]] bool collect_targets(Storage& storage) noexcept;

void offer(MatchState& state, std::string_view text) noexcept;

void offer_blob(MatchState& state, std::span<const std::byte> blob) noexcept;

[[nodiscard]] MatchState match_state(Storage& storage) noexcept;

void merge(Storage& storage, std::span<const names_state::Name> rows) noexcept;

[[nodiscard]] bool add_tag(Storage& storage, std::uint32_t tag) noexcept;

void finish(Storage& storage) noexcept;

} // namespace dawn::client::content::hash_names
