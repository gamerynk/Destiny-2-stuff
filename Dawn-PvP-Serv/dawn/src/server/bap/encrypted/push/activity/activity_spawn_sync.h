#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::server::bap::encrypted::push::activity::spawn_sync {

inline constexpr std::uint64_t kTransitionWindowMs = 15'000;

enum class ArmOutcome : std::uint8_t {
    invalid,
    armed,
    rearmed,
    suppressed,
};

[[nodiscard]] ArmOutcome arm(std::uint64_t platformId,
                             std::uint64_t nowTick,
                             bool join,
                             std::uint64_t activitySessionId) noexcept;

[[nodiscard]] bool acknowledge_join(std::uint64_t platformId,
                                    std::uint64_t activitySessionId) noexcept;

[[nodiscard]] bool awaiting_client_sync(std::uint64_t platformId) noexcept;

[[nodiscard]] std::uint64_t revision(std::uint64_t platformId) noexcept;

void reset() noexcept;

} // namespace dawn::server::bap::encrypted::push::activity::spawn_sync
