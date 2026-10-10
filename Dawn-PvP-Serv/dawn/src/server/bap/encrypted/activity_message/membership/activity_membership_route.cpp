#include "activity_membership_route.h"

#include <array>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/bap/activity_message/activity_membership_acknowledgement_parser.h"
#include "../../../../../middleware/bap/activity_message/activity_client_identity_parser.h"
#include "../../../../../middleware/bap/activity_message/activity_state_refresh_parser.h"
#include "../../../../../middleware/bap/activity_message/client_authoritative_data.h"
#include "../../../../../state/account/account_state.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../gameplay/group/group_host_sessions.h"

namespace dawn::server::bap::encrypted::activity_message::membership {
namespace {

namespace service = middleware::bap::activity_message;
namespace membership_state = state::activity::membership;

[[nodiscard]] membership_state::Identity
make_identity(const service::client_identity::ClientIdentity& parsed,
              std::uint64_t effectiveMemberKey) noexcept {
    const state::AccountState account = state::account_snapshot();
    membership_state::Identity identity{};
    identity.memberKey = effectiveMemberKey;
    identity.smallOpaque = parsed.field1;
    identity.signedOpaque = parsed.field2;
    identity.joinIdentity = parsed.field3;
    // the second (foreign) instance sends this identity with account and character lanes clear; reflecting them would wipe the identity its join seeded
    identity.accountSoid = parsed.accountSoid != 0 ? parsed.accountSoid : account.primarySoid;
    identity.opaqueSoid = parsed.field5 != 0
                              ? parsed.field5
                              : state::account::selected_character_soid(account);
    identity.secondaryOpaque = parsed.field6;
    return identity;
}

[[nodiscard]] membership_state::AuthoritativeUpdate make_authoritative(
    const service::client_authoritative_data::ClientAuthoritativeData& parsed) noexcept {
    membership_state::AuthoritativeUpdate update{};
    update.peerConnection=parsed.peerConnection;
    update.transitionToken = parsed.transitionToken;
    update.hasTransitionToken = parsed.hasTransitionToken;
    update.spawn.state = parsed.spawn.state;
    update.spawn.opaqueByte = parsed.spawn.opaqueByte;
    update.spawn.opaqueValue = parsed.spawn.opaqueValue;
    update.hasSpawn = parsed.hasSpawn;
    update.teleport.state = parsed.teleport.state;
    update.teleport.token = parsed.teleport.token;
    update.teleport.sliceSetIndex = parsed.teleport.sliceSetIndex;
    update.teleport.sliceSetHash = parsed.teleport.sliceSetHash;
    update.hasTeleport = parsed.hasTeleport;
    // the region names the player's bubble; dropping it leaves the host on the arrival slice set and no crossing grants authority
    update.region.index = parsed.region.index;
    update.region.hash = parsed.region.hash;
    update.hasRegion = parsed.hasRegion;
    return update;
}

void report_identity(const service::client_identity::ClientIdentity& parsed) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=activity stage=identity result=ok key=0x%llX field1=%d field2=%d "
                      "field3=0x%llX acct=0x%llX character=0x%llX field6=0x%llX",
                      static_cast<unsigned long long>(parsed.memberKey),
                      static_cast<int>(parsed.field1),
                      static_cast<int>(parsed.field2),
                      static_cast<unsigned long long>(parsed.field3),
                      static_cast<unsigned long long>(parsed.accountSoid),
                      static_cast<unsigned long long>(parsed.field5),
                      static_cast<unsigned long long>(parsed.field6));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

bool prepare_identity(const service::Request& request, ActivityPlan& plan) noexcept {
    service::client_identity::ClientIdentity parsed{};
    if (!service::client_identity::parse_client_identity(request.payload, parsed)) {
        return false;
    }
    report_identity(parsed);

    std::uint64_t effectiveMemberKey = parsed.memberKey;
    const std::uint64_t sourceSessionId =
        gameplay::group::source_activity_session_for_host(request.accountHandle);
    if (sourceSessionId != state::activity::kAbsentSessionId) {
        const std::uint64_t committedMemberKey =
            membership_state::member_key(request.accountHandle);
        if (committedMemberKey != 0) {
            effectiveMemberKey = committedMemberKey;
        }
    }
    if (!membership_state::prepare_identity(
            request.accountHandle,
            make_identity(parsed, effectiveMemberKey),
            plan.membershipMutation)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=activity stage=identity result=refused");
        return false;
    }
    plan.sessionId = request.accountHandle;
    plan.delivery =
        plan.membershipMutation.changesState ? Delivery::membershipNotification : Delivery::none;
    plan.mutationDomain = MutationDomain::membership;
    return true;
}

bool prepare_authoritative(const service::Request& request, ActivityPlan& plan) noexcept {
    service::client_authoritative_data::ClientAuthoritativeData parsed{};
    if (!service::client_authoritative_data::parse_client_authoritative_data(request.payload,
                                                                             parsed)
        || !membership_state::prepare_authoritative(
            request.accountHandle, make_authoritative(parsed), plan.membershipMutation)) {
        return false;
    }
    plan.sessionId = request.accountHandle;
    plan.regionMoved = plan.membershipMutation.movesRegion;
    plan.transitionStarted = plan.membershipMutation.movesTransitionToken;
    // a region move sends the roster even if host state didn't change; the new bubble has no authority until the roster grants it
    plan.delivery = plan.membershipMutation.hasSnapshot || plan.regionMoved
                        ? Delivery::authoritativeNotifications
                        : Delivery::none;
    plan.mutationDomain = MutationDomain::membership;
    return true;
}

bool prepare_refresh(const service::Request& request, ActivityPlan& plan) noexcept {
    service::state_refresh::StateRefresh parsed{};
    if (!service::state_refresh::parse_state_refresh(request.payload, parsed)
        || !membership_state::prepare_refresh(request.accountHandle,
                                              parsed.membershipRevision,
                                              parsed.bubbleIndex,
                                              plan.membershipMutation)) {
        return false;
    }
    plan.sessionId = request.accountHandle;
    // a refresh asks for the whole host snapshot; membership alone leaves it half answered
    plan.delivery = Delivery::refreshNotifications;
    plan.mutationDomain = MutationDomain::membership;
    return true;
}

bool prepare_acknowledgement(const service::Request& request, ActivityPlan& plan) noexcept {
    service::membership_acknowledgement::MembershipAcknowledgement parsed{};
    if (!service::membership_acknowledgement::parse_membership_acknowledgement(request.payload,
                                                                               parsed)
        || !membership_state::prepare_acknowledgement(
            request.accountHandle, parsed.membershipRevision, plan.membershipMutation)) {
        return false;
    }
    plan.sessionId = request.accountHandle;
    plan.delivery = Delivery::none;
    plan.mutationDomain = MutationDomain::membership;
    return true;
}

} // namespace dawn::server::bap::encrypted::activity_message::membership
