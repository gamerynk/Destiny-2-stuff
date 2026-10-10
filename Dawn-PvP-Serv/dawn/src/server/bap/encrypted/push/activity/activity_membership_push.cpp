#include "activity_membership_push.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include "../../../../../core/logging/log.h"
#include "../../../../../state/activity/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../gameplay/endpoint/gameplay_endpoint.h"
#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/account_store.h"
#endif

#include "../../../../../core/runtime/runtime_policy.h"
#include "../../../../../middleware/bap/activity_message/replicate_membership.h"
#include "../../../../../middleware/bap/activity_message/activity_player_assignment.h"
#include "../../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../../gameplay/gameplay_advertisement.h"
#include "activity_arrival.h"
#include "activity_notification_frame.h"
#include "../../../../../state/activity/shared/runtime.h"
#include "../../../../../state/activity/crucible/native_modes.h"
#include "../../../../gameplay/group/group_host_sessions.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace message = middleware::bap::activity_message::replicate_membership;


[[nodiscard]] message::MembershipSnapshot
make_wire_snapshot(std::uint64_t sessionId,
                   const state::activity::membership::PendingMutation& mutation,
                   bool includeCitizenAdvertisement) noexcept {
    const state::activity::membership::Snapshot& snapshot = mutation.snapshot;
    message::MembershipSnapshot wire{};
    state::activity::destination::DestinationSelection selection{};
    if (state::activity::destination::snapshot(sessionId,selection)) {
        wire.competitiveTeams=middleware::bap::activity_message::player_assignment::competitive_destination(
            std::string_view(reinterpret_cast<const char*>(selection.packageName.data()),selection.packageNameLength));
        if (const auto* mode=state::activity::crucible::find_native_mode(selection.previousActivityIndex))
            wire.teamMode=middleware::bap::activity_message::player_assignment::mode_for(mode->symbolicName);
    }
    wire.peerConnections[0]=snapshot.peerConnection;
    wire.identity.memberKey = snapshot.identity.memberKey;
    wire.identity.field1 = snapshot.identity.smallOpaque;
    wire.identity.field2 = snapshot.identity.signedOpaque;
    wire.identity.field3 = snapshot.identity.joinIdentity;
    wire.identity.accountSoid = snapshot.identity.accountSoid;
    wire.identity.field5 = snapshot.identity.opaqueSoid;
    wire.identity.field6 = snapshot.identity.secondaryOpaque;
    for (std::size_t i=0;i<snapshot.reservations.size();++i) {
        const auto& row=snapshot.reservations[i];
        auto& target=wire.reservations[i];
        target.memberKey=row.memberKey; target.field1=row.smallOpaque;
        target.field2=row.signedOpaque; target.field3=row.joinIdentity;
        target.accountSoid=row.accountSoid; target.field5=row.opaqueSoid;
        target.field6=row.secondaryOpaque;
    }
    wire.spawn.state = snapshot.spawn.state;
    wire.spawn.opaqueByte = snapshot.spawn.opaqueByte;
    wire.spawn.opaqueValue = snapshot.spawn.opaqueValue;
    wire.teleport.state = snapshot.teleport.state;
    wire.teleport.token = snapshot.teleport.token;
    wire.teleport.sliceSetIndex = snapshot.teleport.sliceSetIndex;
    wire.teleport.sliceSetHash = snapshot.teleport.sliceSetHash;
    wire.revision = snapshot.revision;
    wire.epoch = snapshot.epoch;
    wire.transitionToken = snapshot.transitionToken;
    state::activity::shared::Projection shared{};
    const auto peerSource=server::gameplay::group::source_activity_session_for_host(sessionId);
    const bool hasShared=state::activity::shared::project(sessionId,snapshot,shared,peerSource);
    if (hasShared) {
        wire.localMemberSlot=shared.localSlot;
        wire.hostMemberKey=shared.hostKey;
        wire.memberTransitionTokens=shared.tokens;
        wire.hasMemberTransitionTokens=true;
        wire.reservationField3WireWordMask=shared.identityWireWordMask;
        wire.joinedMemberMask=shared.joinedMemberMask;
        wire.peerConnections=shared.peerConnections;
        wire.spawn={shared.spawn.state,shared.spawn.opaqueByte,shared.spawn.opaqueValue};
        wire.teleport={shared.teleport.state,shared.teleport.token,
            shared.teleport.sliceSetIndex,shared.teleport.sliceSetHash};
        for (std::size_t i=0;i<31;++i) {
            const auto source=i+1==shared.localSlot?0:i+1;
            const auto& row=shared.members[source];
            wire.reservations[i]={row.memberKey,row.smallOpaque,row.signedOpaque,
                row.joinIdentity,row.accountSoid,row.opaqueSoid,row.secondaryOpaque};
        }
    }
#if defined(RESTORATION_DEDICATED_SERVER)
    for(std::size_t slot=0;slot<wire.players.size();++slot) {
        if(slot!=wire.localMemberSlot && !(wire.joinedMemberMask&(std::uint32_t{1}<<slot))) continue;
        const auto source=slot==0?wire.localMemberSlot:slot;
        const auto& identity=slot==wire.localMemberSlot?wire.identity:wire.reservations[source-1];
        if(!identity.memberKey || !identity.accountSoid) continue;
        std::uint64_t platform{};
        std::array<char,::restoration::account_store::kAccountNameCapacity> name{};
        if(!::restoration::account_store::presentation_by_account(identity.accountSoid,platform,name)) continue;
        const auto end=std::find(name.begin(),name.end(),'\0');
        const auto length=static_cast<std::size_t>(end-name.begin());
        if(!length || length>=wire.players[slot].name.size()) continue;
        wire.players[slot].platformId=platform;
        for(std::size_t i=0;i<length;++i)
            wire.players[slot].name[i]=static_cast<unsigned char>(name[i]);
    }
#endif
    wire.peerConnections[wire.localMemberSlot].playerTeam=snapshot.peerConnection.playerTeam;
    wire.publishPeerConnections=(peerSource==0 || hasShared) && message::reservation_count(wire)!=0;
    // the region this body is about to commit, not the one State holds; staging runs first and the old region would leave the pending record empty for good
    const EffectiveRegion region = planned_region(mutation, sessionId);
    if (includeCitizenAdvertisement) {
        server::gameplay::build_advertisement(
            sessionId,
            region.index,
            region.reported ? server::gameplay::RegionSource::reported
                            : server::gameplay::RegionSource::arrival,
            wire.localMemberSlot,
            wire.citizen);
    }
    return wire;
}

} // namespace

bool append_membership_notification(Scratch& scratch,
                                    const activity_message::ActivityPlan& activity,
                                    std::span<const std::byte, state::kAesKeySize> key,
                                    std::array<std::byte, state::kBapNonceSize>& nonce,
                                    std::span<std::byte> response,
                                    std::size_t& written,
                                    bool includeCitizenAdvertisement) noexcept {
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        return false;
    }
    if (written > response.size() || !activity.membershipMutation.hasSnapshot) {
        return false;
    }

    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::size_t messageSize = 0;
    const message::MembershipSnapshot snapshot =
        make_wire_snapshot(
            activity.sessionId, activity.membershipMutation, includeCitizenAdvertisement);
    const bool encoded =
        message::encode_replicate_membership(snapshot, scratch.responseBody, messageSize)
        && append_notification_frame(scratch,
                                     activity.sessionId,
                                     message::kMessageType,
                                     std::span(scratch.responseBody).first(messageSize),
                                     key,
                                     nonce,
                                     response,
                                     written);
    SecureZeroMemory(scratch.responseBody.data(),
        (std::min)(scratch.responseBody.size(), message::encoded_size(snapshot)));
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
    } else {
        if (written > initialWritten) {
            SecureZeroMemory(response.data() + initialWritten, written - initialWritten);
        }
        written = initialWritten;
        nonce = initialNonce;
    }
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

} // namespace dawn::server::bap::encrypted::push::activity
