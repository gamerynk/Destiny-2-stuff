#pragma once

#include <cstdint>

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] constexpr bool activity_allows_join_in_progress(
    bool regionReported, std::int32_t region, std::uint64_t spawnSyncRevision,
    bool awaitingClientSync) noexcept {
    return regionReported && region >= 0 && spawnSyncRevision != 0 && !awaitingClientSync;
}

} // namespace dawn::server::bap::encrypted::push::activity
