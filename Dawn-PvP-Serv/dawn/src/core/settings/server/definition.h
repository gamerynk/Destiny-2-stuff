#pragma once

#include "../../../state/entitlements/definition.h"
#include "gameplay/definition.h"

namespace dawn::core::settings::server {

inline constexpr std::uint16_t kDefaultBapPort = 30974;
inline constexpr std::uint16_t kDefaultHttpPort = 30975;
inline constexpr std::uint16_t kDefaultPublicHttpPort = 0;

struct Settings {
    state::entitlements::Table entitlements{};
    gameplay::Settings gameplay{};
    // the listener binds this and SignOn publishes it; zero means no relay
    std::uint16_t bapPort{kDefaultBapPort};
    std::array<unsigned char, 4> bapAdvertisedAddress{127, 0, 0, 1};
    bool hasBapAdvertisedAddress{};
    std::uint16_t bapAdvertisedPort{};
    std::uint16_t httpPort{kDefaultHttpPort};
    std::uint16_t publicHttpPort{kDefaultPublicHttpPort};
    // hosts can't extract build data, so the cache comes from a client; the fingerprint
    // derives from the authored account, which a client lacks, so a good cache reads as stale
    std::uint64_t buildDataEquipmentHash{};
    bool hasBuildDataEquipmentHash{};
};

} // namespace dawn::core::settings::server
