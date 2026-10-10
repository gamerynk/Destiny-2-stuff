#include <array>

#include "../../patterns/game.h"
#include "network.h"
#include "network/network_content_derivation.h"
#include "relative.h"
#include "resolution/internal.h"

namespace dawn::client::targets::game::network {
namespace {

Targets g_targets;

[[nodiscard]] constexpr std::size_t index(patterns::game::Id id) noexcept {
    return static_cast<std::size_t>(id);
}

// the token load's rip-relative displacement starts inside its lea (14), ends at 18
constexpr std::size_t kContentIdTokenDisplacementOffset = 14;
constexpr std::size_t kContentIdTokenInstructionEndOffset = 18;

} // namespace

bool derive(std::span<const patterns::ImageRange> image,
            std::span<const patterns::Match> matches,
            Targets& output) noexcept {
    output = {};
    if (matches.size() != resolution::kNetworkMatchCount) {
        return false;
    }

    Targets resolved;
    std::byte* const contentManifestGate =
        matches[index(patterns::game::Id::contentManifestGate)].address;
    resolved.bubbleAuthorityDecoder =
        matches[index(patterns::game::Id::bubbleAuthorityDecoder)].address;
    resolved.contentUntrackedGetter =
        matches[index(patterns::game::Id::contentUntrackedGetter)].address;
    std::byte* const contentIdTokenLoad =
        matches[index(patterns::game::Id::contentIdTokenLoad)].address;
    if (!relative::resolve(contentIdTokenLoad,
                           kContentIdTokenDisplacementOffset,
                           kContentIdTokenInstructionEndOffset,
                           resolved.contentIdToken)
        || !content_derivation::derive(image, contentManifestGate, resolved)) {
        return false;
    }
    output = resolved;
    return true;
}

void publish(const Targets& targets) noexcept {
    g_targets = targets;
}

bool resolve(std::span<const patterns::ImageRange> image) noexcept {
    g_targets = {};
    const auto definitions = patterns::game::definitions().first(resolution::kNetworkMatchCount);
    std::array<patterns::Match, resolution::kNetworkMatchCount> matches{};
    if (!patterns::resolve_all(image, definitions, matches)) {
        return false;
    }
    for (const patterns::Match match : matches) {
        if (match.status != patterns::MatchStatus::unique) {
            return false;
        }
    }

    Targets resolved;
    if (!derive(image, matches, resolved)) {
        return false;
    }
    publish(resolved);
    return true;
}

void clear() noexcept {
    g_targets = {};
}

const Targets& get() noexcept {
    return g_targets;
}

} // namespace dawn::client::targets::game::network
