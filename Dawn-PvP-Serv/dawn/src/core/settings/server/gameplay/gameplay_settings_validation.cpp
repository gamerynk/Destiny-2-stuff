#include "../../../../state/activity/entity_slots/definition.h"
#include "definition.h"

namespace dawn::core::settings::server::gameplay {

namespace {

[[nodiscard]] bool reserve_fits(std::uint16_t reserve) noexcept {
    constexpr std::size_t kSlotCount = state::activity::entity_slots::kSlotCount;
    if (reserve < kMinimumServerReserve || static_cast<std::size_t>(reserve) >= kSlotCount) {
        return false;
    }
    return kSlotCount - static_cast<std::size_t>(reserve) >= kClientLeaseMinimum;
}

} // namespace

bool valid(const Settings& settings) noexcept {
    if (settings.topology == Topology::disabled) {
        return true;
    }
    if (settings.port == 0 || settings.port % kPortAlignment != 0) {
        return false;
    }
    if (settings.port == kDiscoveryPortLow || settings.port == kDiscoveryPortHigh) {
        return false;
    }
    return reserve_fits(settings.serverReserveCount);
}

std::uint16_t effective_reserve(const Settings& settings) noexcept {
    return settings.topology == Topology::disabled ? 0 : settings.serverReserveCount;
}

} // namespace dawn::core::settings::server::gameplay
