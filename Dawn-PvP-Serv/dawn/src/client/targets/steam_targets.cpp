#include "steam_targets.h"

#include <array>

#include "../patterns/steam.h"

namespace dawn::client::targets::steam {
namespace {

Targets g_targets;

}

bool resolve(std::span<const patterns::ImageRange> image) noexcept {
    g_targets = {};
    const auto definitions = patterns::steam::definitions();
    std::array<patterns::Match, static_cast<std::size_t>(patterns::steam::Id::count)> matches{};
    if (!patterns::resolve_all(image, definitions, matches)) {
        return false;
    }
    for (const patterns::Match match : matches) {
        if (match.status != patterns::MatchStatus::unique) {
            return false;
        }
    }

    Targets resolved;
    resolved.authenticationStatus =
        matches[static_cast<std::size_t>(patterns::steam::Id::authenticationStatus)].address;
    resolved.setCertificate =
        matches[static_cast<std::size_t>(patterns::steam::Id::setCertificate)].address;
    // publish only after every required address has been validated
    g_targets = resolved;
    return true;
}

void clear() noexcept {
    g_targets = {};
}

const Targets& get() noexcept {
    return g_targets;
}

} // namespace dawn::client::targets::steam
