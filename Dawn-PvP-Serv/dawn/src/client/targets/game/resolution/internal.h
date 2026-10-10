#pragma once

#include <cstddef>
#include <span>

#include "../../../patterns/game.h"
#include "../assert_handler/game_assert_targets.h"
#include "../config_getter/game_config_getter_targets.h"
#include "../content.h"
#include "../network.h"
#include "../packages/game_package_targets.h"
#include "../retail_log.h"

namespace dawn::client::targets::game::resolution {

inline constexpr std::size_t kNetworkMatchCount =
    static_cast<std::size_t>(patterns::game::Id::contentIdTokenLoad) + 1;
inline constexpr std::size_t kContentFirstMatch =
    static_cast<std::size_t>(patterns::game::Id::queuezObjectResolver);
inline constexpr std::size_t kMatchCount = static_cast<std::size_t>(patterns::game::Id::count);
inline constexpr std::size_t kRetailLogFirstMatch =
    static_cast<std::size_t>(patterns::game::Id::retailLogEnqueue);
inline constexpr std::size_t kContentMatchCount = kRetailLogFirstMatch - kContentFirstMatch;
inline constexpr std::size_t kRetailLogMatchCount = kMatchCount - kRetailLogFirstMatch;
// only these signatures gate activation; the retail-log slice is diagnostic and may miss
inline constexpr std::size_t kRequiredMatchCount = kRetailLogFirstMatch;

static_assert(kNetworkMatchCount == kContentFirstMatch);
static_assert(kContentFirstMatch + kContentMatchCount == kRetailLogFirstMatch);

} // namespace dawn::client::targets::game::resolution

namespace dawn::client::targets::game::network {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image,
                          std::span<const patterns::Match> matches,
                          Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::network

namespace dawn::client::targets::game::content {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image,
                          std::span<const patterns::Match> matches,
                          Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::content

namespace dawn::client::targets::game::retail_log {

[[nodiscard]] bool derive(std::span<const patterns::Match> matches, Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::retail_log
