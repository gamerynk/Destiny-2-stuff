#include <algorithm>
#include <array>
#include <cstdio>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family6/family6_encoder.h"
#include "../../../../../state/social/social_state.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/account_store.h"
#include "../../../../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

namespace family6 = middleware::datagen::family6;

// the details component reads Family-6 slot zero before Family-2; its native update (RVA 0x1574A60) tolerates an absent party but not an absent anchor
[[nodiscard]] bool prepare_account_anchor(
    Scratch& scratch,
    const middleware::queuez::Subscription& subscription,
    const Reservation& reservation,
    Prepared& prepared,
    std::uint64_t authorizedRoot) noexcept {
    if (subscription.familyType != kFireteamFamilyType || authorizedRoot == 0
        || subscription.familyRootSoid != authorizedRoot
        || reservation.rawWriteOffset > scratch.plaintext.size()
        || family6::kAnchorSize > scratch.plaintext.size() - reservation.rawWriteOffset) {
        return report_failure("family6_anchor_scope");
    }
    const auto anchor = std::span(scratch.plaintext).subspan(
        reservation.rawWriteOffset, family6::kAnchorSize);
    if (!family6::encode_anchor(authorizedRoot, authorizedRoot, anchor)) {
        return report_failure("family6_anchor_encode");
    }
    Prepared staged{};
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    if (!append_object(scratch, anchor, middleware::datagen::kFireteamAnchorObjectId,
                       authorizedRoot, staged.objects[0], compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("family6_anchor_object");
    }
    staged.rawClearSize = (std::max)(
        reservation.rawClearSize, reservation.rawWriteOffset + family6::kAnchorSize);
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType, subscription.familyRootSoid, kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag, std::span(staged.objects).first(1)};
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("family6_anchor_commit");
    }
    core::log::write(core::log::Channel::server, core::log::Level::info,
                     "ev=queuez stage=family6_anchor result=ok objects=1 bytes=16");
    return true;
}

void report_remote(const middleware::queuez::Subscription& subscription,
                   const state::social::RemoteAccount* remote) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = remote != nullptr
                          ? std::snprintf(
                                line.data(),
                                line.size(),
                                "ev=queuez stage=family6_remote result=resolved"
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
                                          "ev=queuez stage=family6_remote result=unavailable"
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

bool prepare_fireteam(Scratch& scratch,
                      const middleware::queuez::Subscription& subscription,
                      const Reservation& reservation,
                      Prepared& prepared,
                      const state::social::RemoteAccount* liveRemote,
                      std::uint64_t authenticatedSelfRoot) noexcept {
    if (authenticatedSelfRoot != 0) {
        // ambient remote-profile scope is never proof of self authorization
        if (state::account_snapshot().primarySoid != authenticatedSelfRoot) {
            return report_failure("family6_self_scope");
        }
#if defined(RESTORATION_DEDICATED_SERVER)
        middleware::social::realtime::Lobby selfLobby{};
        const bool haveLobby =
            ::restoration::realtime_store::lobby_for_account(authenticatedSelfRoot, selfLobby);
        core::log::writef(core::log::Channel::server,
                          core::log::Level::info,
                          "ev=fireteam_probe stage=self root=0x%016llX result=%s lobby=0x%016llX"
                          " members=%zu owner=0x%016llX joinable=%d max=%d",
                          static_cast<unsigned long long>(authenticatedSelfRoot),
                          haveLobby ? "lobby_found" : "no_lobby",
                          static_cast<unsigned long long>(selfLobby.id),
                          selfLobby.memberCount,
                          static_cast<unsigned long long>(selfLobby.ownerPlatformId),
                          selfLobby.joinable ? 1 : 0,
                          static_cast<int>(selfLobby.maximumMembers));
#endif
        return prepare_account_anchor(
            scratch, subscription, reservation, prepared, authenticatedSelfRoot);
    }
    report_remote(subscription, liveRemote);
    if (subscription.familyType != kFireteamFamilyType
        || subscription.familyRootSoid == 0
        || liveRemote == nullptr || liveRemote->accountSoid != subscription.familyRootSoid
        || liveRemote->relationship != state::social::Relationship::friendAccount
        || liveRemote->platformId == 0
        || reservation.rawWriteOffset > scratch.plaintext.size()) {
        return report_failure("family6_remote");
    }
    const state::social::RemoteAccount& remote = *liveRemote;
    if (remote.lobbyId == 0 || !remote.joinable) {
        return prepare_account_anchor(
            scratch, subscription, reservation, prepared, remote.accountSoid);
    }
    const family6::GraphKeys graph = family6::account_linked_graph(remote.accountSoid);
    if (!family6::valid_graph(graph, remote.lobbyId)) {
        return report_failure("family6_graph");
    }

    constexpr std::size_t kTotalSize = family6::kAnchorSize + family6::kPartySize;
    const auto raw = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (raw.size() < kTotalSize) {
        return report_failure("family6_storage");
    }
    const auto anchor = raw.first(family6::kAnchorSize);
    const auto party = raw.subspan(family6::kAnchorSize, family6::kPartySize);

    std::array<family6::Member, family6::kMemberCapacity> members{};
    std::size_t memberCount = 0;
#if defined(RESTORATION_DEDICATED_SERVER)
    middleware::social::realtime::Lobby lobby{};
    if (!::restoration::realtime_store::lobby_for_member(remote.platformId, lobby)
        || lobby.id != remote.lobbyId || lobby.ownerPlatformId == 0 || lobby.memberCount == 0
        || lobby.memberCount > members.size()) {
        return report_failure("family6_lobby_roster");
    }
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        const middleware::social::realtime::Member& lobbyMember = lobby.members[index];
        state::social::RemoteAccount resolved{};
        const state::social::RemoteAccount* projection = nullptr;
        if (lobbyMember.accountSoid == remote.accountSoid) {
            projection = &remote;
        } else if (::restoration::account_store::live_friend(
                       remote.accountSoid, lobbyMember.accountSoid, resolved)) {
            projection = &resolved;
        } else {
            // a fireteam member need not be friends with the viewed player; requiring it failed the party record from the fourth player on
            std::uint64_t platform = 0;
            std::uint64_t revision = 0;
            state::AccountState profile{};
            std::array<char, ::restoration::account_store::kAccountNameCapacity> name{};
            if (::restoration::account_store::profile_by_account(
                    lobbyMember.accountSoid, platform, profile, revision, name)
                && platform == lobbyMember.platformId) {
                resolved = {};
                resolved.platformId = platform;
                resolved.accountSoid = lobbyMember.accountSoid;
                const auto nameEnd = std::find(name.begin(), name.end(), '\0');
                const std::size_t length = (std::min)(
                    static_cast<std::size_t>(nameEnd - name.begin()),
                    resolved.displayName.size() - 1U);
                std::copy_n(name.begin(), length, resolved.displayName.begin());
                projection = &resolved;
            }
        }
        if (projection == nullptr || lobbyMember.platformId == 0 || lobbyMember.accountSoid == 0
            || projection->platformId != lobbyMember.platformId
            || projection->accountSoid != lobbyMember.accountSoid) {
            return report_failure("family6_lobby_member");
        }
        family6::Member& member = members[memberCount++];
        member.accountSoid = projection->accountSoid;
        member.emblemDefinition = projection->emblemDefinition;
        member.emblemVariant = projection->emblemVariant;
        member.displayName = projection->displayName;
        member.status = family6::kFireteamMemberStatus;
        if (lobbyMember.platformId == lobby.ownerPlatformId) {
            member.status |= family6::kPartyLeaderStatus;
        }
    }
#else
    family6::Member& owner = members[memberCount++];
    owner.accountSoid = remote.accountSoid;
    owner.emblemDefinition = remote.emblemDefinition;
    owner.emblemVariant = remote.emblemVariant;
    owner.displayName = remote.displayName;
    owner.status = family6::kFireteamMemberStatus | family6::kPartyLeaderStatus;
#endif
    if (!family6::encode_anchor(graph.accountRootSoid,
                                 graph.family6AnchorLinkSoid,
                                 anchor)
        || !family6::encode_party(graph.family6PartyLeadingSoid,
                                  std::span<const family6::Member>(members).first(memberCount),
                                  party)) {
        return report_failure("family6_encode");
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + kTotalSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t objectCount = 0;
    if (!append_object(scratch,
                       anchor,
                       middleware::datagen::kFireteamAnchorObjectId,
                       graph.accountRootSoid,
                       staged.objects[objectCount++],
                       compressedExtent)
        || !append_object(scratch,
                          party,
                          middleware::datagen::kFireteamPartyObjectId,
                          graph.family6PartyObjectSoid,
                          staged.objects[objectCount++],
                          compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("family6_object");
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
        return report_failure("family6_commit");
    }

    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=queuez stage=family6 result=ok root=0x%016llX linked=0x%016llX"
        " lobby=0x%016llX objects=%zu members=%zu bytes=%zu joinable=1",
        static_cast<unsigned long long>(graph.accountRootSoid),
        static_cast<unsigned long long>(graph.family6AnchorLinkSoid),
        static_cast<unsigned long long>(remote.lobbyId),
        objectCount,
        memberCount,
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
