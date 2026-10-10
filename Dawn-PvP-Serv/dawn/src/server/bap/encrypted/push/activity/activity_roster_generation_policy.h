#pragma once

#include <cstdint>

namespace dawn::server::bap::encrypted::push::activity::roster_generation {

inline constexpr std::uint8_t kWarmupSends = 3;
inline constexpr std::uint8_t kEstablishedSends = kWarmupSends + 1;

[[nodiscard]] inline constexpr bool should_advance(std::uint8_t sends,
                                                   std::uint32_t previousGroups,
                                                   std::uint32_t nextGroups,
                                                   bool burst,
                                                   bool clientSyncKnown,
                                                   bool previousClientSync,
                                                   bool nextClientSync) noexcept {
    const bool clientSyncChanged =
        clientSyncKnown && previousClientSync != nextClientSync;
    return sends < kWarmupSends || (!burst && previousGroups != nextGroups)
           || (sends < kEstablishedSends && clientSyncChanged);
}

[[nodiscard]] inline constexpr std::uint8_t next_sends(std::uint8_t sends,
                                                      bool nextClientSync) noexcept {
    if (sends < kWarmupSends) {
        return static_cast<std::uint8_t>(sends + 1);
    }
    return !nextClientSync || sends >= kEstablishedSends ? kEstablishedSends : kWarmupSends;
}

} // namespace dawn::server::bap::encrypted::push::activity::roster_generation
