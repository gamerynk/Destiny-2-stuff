#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family7/family7_encoder.h"
#include "../../../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../../../state/social/social_state.h"
#include "internal.h"
#include "snapshot_storage.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

namespace family7 = middleware::datagen::family7;

std::atomic<JoinDescriptorProvider> g_joinDescriptorProvider{};

void report_remote(const middleware::queuez::Subscription& subscription,
                   const state::social::RemoteAccount* remote) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = remote != nullptr
                          ? std::snprintf(
                                line.data(),
                                line.size(),
                                "ev=queuez stage=family7_remote result=resolved"
                                " root=0x%016llX account=0x%016llX platform=0x%016llX"
                                " lobby=0x%016llX relationship=%d joinable=%d",
                                static_cast<unsigned long long>(subscription.familyRootSoid),
                                static_cast<unsigned long long>(remote->accountSoid),
                                static_cast<unsigned long long>(remote->platformId),
                                static_cast<unsigned long long>(remote->lobbyId),
                                static_cast<int>(remote->relationship),
                                remote->joinable ? 1 : 0)
                          : std::snprintf(line.data(),
                                          line.size(),
                                          "ev=queuez stage=family7_remote result=unavailable"
                                          " root=0x%016llX",
                                          static_cast<unsigned long long>(
                                              subscription.familyRootSoid));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         remote != nullptr ? core::log::Level::info : core::log::Level::warn,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
    }
}

} // namespace

void set_join_descriptor_provider(JoinDescriptorProvider provider) noexcept {
    g_joinDescriptorProvider.store(provider, std::memory_order_release);
}

bool prepare_join(Scratch& scratch,
                  const middleware::queuez::Subscription& subscription,
                  const Reservation& reservation,
                  Prepared& prepared,
                  const state::social::RemoteAccount* liveRemote) noexcept {
    report_remote(subscription, liveRemote);
    if (subscription.familyType != kJoinFamilyType || subscription.familyRootSoid == 0
        || liveRemote == nullptr || liveRemote->accountSoid != subscription.familyRootSoid
        || liveRemote->relationship != state::social::Relationship::friendAccount
        || liveRemote->platformId == 0 || liveRemote->lobbyId == 0 || !liveRemote->joinable
        || reservation.rawWriteOffset > scratch.plaintext.size()) {
        return report_failure("family7_remote");
    }
    const state::social::RemoteAccount& remote = *liveRemote;
    JoinDescriptor joinDescriptor{};
    // family 7 is the native network-resolution boundary; use the descriptor captured from the group-zero owner
    // the central server's method-0 endpoint would create a second native host and split the clients' membership graphs
    const JoinDescriptorProvider provider =
        g_joinDescriptorProvider.load(std::memory_order_acquire);
    // the descriptor belongs to the fireteam leader, not the member being viewed; passing the viewed friend failed unless they led
    std::uint64_t ownerPlatformId = remote.platformId;
#if defined(RESTORATION_DEDICATED_SERVER)
    middleware::social::realtime::Lobby lobby{};
    if (::restoration::realtime_store::lobby_for_member(remote.platformId, lobby)
        && lobby.id == remote.lobbyId && lobby.ownerPlatformId != 0) {
        ownerPlatformId = lobby.ownerPlatformId;
    }
#endif
    if (provider == nullptr
        || !provider(remote.lobbyId, ownerPlatformId, joinDescriptor)) {
        return report_failure("family7_owner_descriptor_not_ready");
    }

    constexpr std::size_t kTotalSize = family7::kAnchorSize + family7::kBodySize;
    const auto raw = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (raw.size() < kTotalSize) {
        return report_failure("family7_storage");
    }
    const auto anchor = raw.first(family7::kAnchorSize);
    const auto body = raw.subspan(family7::kAnchorSize, family7::kBodySize);
    const std::uint64_t bodySoid = remote.accountSoid;
    if (!family7::encode_anchor(remote.accountSoid, bodySoid, anchor)
        || !family7::encode_body(bodySoid,
                                 joinDescriptor,
                                 family7::kCompatibilitySignature,
                                 body)) {
        return report_failure("family7_encode");
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + kTotalSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t objectCount = 0;
    if (!append_object(scratch,
                       anchor,
                       middleware::datagen::kJoinAnchorObjectId,
                       remote.accountSoid,
                       staged.objects[objectCount++],
                       compressedExtent)
        || !append_object(scratch,
                          body,
                          middleware::datagen::kJoinDescriptorObjectId,
                          bodySoid,
                          staged.objects[objectCount++],
                          compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("family7_object");
    }
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("family7_commit");
    }

    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=queuez stage=family7 result=ok root=0x%016llX body=0x%016llX"
        " owner_platform=0x%016llX lobby=0x%016llX descriptor_source=owner_native"
        " method=%u compatibility=0x%016llX descriptor_bytes=%zu objects=%zu bytes=%zu",
        static_cast<unsigned long long>(remote.accountSoid),
        static_cast<unsigned long long>(bodySoid),
        static_cast<unsigned long long>(remote.platformId),
        static_cast<unsigned long long>(remote.lobbyId),
        std::to_integer<unsigned>(joinDescriptor[93U]),
        static_cast<unsigned long long>(family7::kCompatibilitySignature),
        joinDescriptor.size(),
        objectCount,
        kTotalSize);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
