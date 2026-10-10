#include <algorithm>
#include "activity_membership_route.h"
#include "middleware/bap/activity_message/peer_reservation.h"
#include "state/runtime/runtime.h"
#include "state/matchmaking/social_search.h"
#include "state/activity/membership/activity_membership_query.h"
#include "core/logging/log.h"
#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/account_store.h"
#include "../../../../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::bap::encrypted::activity_message::membership {
namespace reservation=middleware::bap::activity_message::peer_reservation;
namespace stored=state::activity::membership;
namespace {
bool authorize(std::span<const middleware::bap::activity_message::client_identity::ClientIdentity> peers,
    std::uint64_t socialSession,bool requirePartyLeader) noexcept {
#if defined(RESTORATION_DEDICATED_SERVER)
    const auto owner=state::account_snapshot();
    std::uint64_t ownerPlatform{}, revision{};
    state::AccountState profile{};
    std::array<char,::restoration::account_store::kAccountNameCapacity> name{};
    middleware::social::realtime::Lobby lobby{};
    if (!::restoration::account_store::profile_by_account(owner.primarySoid,ownerPlatform,profile,revision,name)
        || !::restoration::realtime_store::lobby_for_member(ownerPlatform,lobby)
        || (requirePartyLeader && lobby.ownerPlatformId!=ownerPlatform)
        || lobby.memberCount>lobby.members.size()) return false;
    // an activity startup must include its authenticated native coordinator; a social startup is no grant to reserve arbitrary peers
    if(!requirePartyLeader && std::count_if(peers.begin(),peers.end(),
        [&](const auto& peer){return peer.accountSoid==owner.primarySoid;})!=1) return false;
    for (const auto& peer:peers) {
        std::uint64_t platform{};
        if (peer.accountSoid==0 || !::restoration::account_store::profile_by_account(
            peer.accountSoid,platform,profile,revision,name)) return false;
        const bool member=std::any_of(lobby.members.begin(),lobby.members.begin()+lobby.memberCount,
            [&](const auto& row){return row.platformId==platform;});
        const bool character=std::any_of(profile.characters.begin(),profile.characters.end(),
            [&](const auto& row){return row.soid!=0 && row.soid==peer.field5;});
        const bool matched=socialSession!=0 && state::matchmaking::social_search_peers(
            owner.primarySoid,peer.accountSoid,socialSession);
        if ((!member && !matched) || !character) return false;
    }
    return true;
#else
    (void)peers;
    (void)socialSession;
    (void)requirePartyLeader;
    return false;
#endif
}
}
bool authorize_reservations(std::span<const middleware::bap::activity_message::client_identity::ClientIdentity> peers,
    std::uint64_t socialSession) noexcept {
    return authorize(peers,socialSession,true);
}
bool authorize_social_startup_reservations(
    std::span<const middleware::bap::activity_message::client_identity::ClientIdentity> peers) noexcept {
    return authorize(peers,0,false);
}
bool prepare_reservations(const middleware::bap::activity_message::Request& request,
                          ActivityPlan& plan) noexcept {
    reservation::Request parsed{};
    reservation::Release released{};
    std::array<stored::Identity,reservation::kCapacity> rows{};
    std::size_t count=0;
    std::uint32_t epoch=0;
    std::uint64_t releaseKey=0;
    if (request.messageType==reservation::kRequestType) {
        if (!reservation::parse(request.payload,parsed)
            || !authorize_reservations(std::span(parsed.peers).first(parsed.count),request.accountHandle)) return false;
        count=parsed.count; epoch=parsed.epoch;
        for (std::size_t i=0;i<count;++i) {
            const auto& peer=parsed.peers[i];
            rows[i]={peer.memberKey,peer.field1,peer.field2,peer.field3,
                     peer.accountSoid,peer.field5,peer.field6};
        }
    } else {
        if (!reservation::parse(request.payload,released) || released.key==0
            || !authorize_reservations({})) return false;
        epoch=released.epoch; releaseKey=released.key;
    }
    if (!stored::prepare_reservations(request.accountHandle,epoch,
        std::span(rows).first(count),releaseKey,plan.membershipMutation)) return false;
    plan.sessionId=request.accountHandle;
    plan.delivery=plan.membershipMutation.changesState?Delivery::membershipNotification:Delivery::none;
    plan.mutationDomain=MutationDomain::membership;
    core::log::write(core::log::Channel::server,core::log::Level::info,
        request.messageType==reservation::kRequestType
            ? "ev=activity stage=peer_reservation result=prepared"
            : "ev=activity stage=peer_reservation_release result=prepared");
    return true;
}
}
