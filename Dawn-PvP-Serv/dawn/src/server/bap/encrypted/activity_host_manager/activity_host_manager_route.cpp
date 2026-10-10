#include "activity_host_manager_route.h"

#include <array>
#include <climits>
#include <cstdio>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/activity_host_manager/request/activity_manager_request.h"
#include "../../../../middleware/bap/activity_host_manager/request/selection/activity_manager_selection_parser.h"
#include "../../../../middleware/bap/activity_host_manager/response/activity_manager_response.h"
#include "../../../../state/activity/defaults/activity_defaults_snapshot.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/build_data/runtime.h"
#include "middleware/bap/activity_host_manager/request/selection/activity_manager_reservations.h"
#include "server/bap/encrypted/activity_message/membership/activity_membership_route.h"
#include "state/activity/shared/social_admission.h"

namespace dawn::server::bap::encrypted::activity_host_manager {
namespace {

namespace request_selection = middleware::bap::activity_host_manager::request::selection;

[[nodiscard]] std::string_view
copy_name(const request_selection::ActivityManagerSelection& source) noexcept {
    return source.hasPackageName
               ? std::string_view(reinterpret_cast<const char*>(source.packageName.data()),
                                  source.packageNameLength)
               : std::string_view{};
}

// field 1 wins; when the copies disagree the one naming a known destination wins, since an unknown name has no bubble layout
[[nodiscard]] const request_selection::ActivityManagerSelection&
choose_copy(const request_selection::ActivityManagerSelectionResult& parsed) noexcept {
    if (!parsed.hasSelection) {
        return parsed.secondary;
    }
    if (!parsed.hasSecondary) {
        return parsed.selection;
    }
    ::dawn::state::build_data::scenarios::Definition layout{};
    const bool primaryKnown =
        ::dawn::state::build_data::find_scenario_layout(copy_name(parsed.selection), layout);
    const bool secondaryKnown =
        ::dawn::state::build_data::find_scenario_layout(copy_name(parsed.secondary), layout);
    return !primaryKnown && secondaryKnown ? parsed.secondary : parsed.selection;
}

void report_selection(const request_selection::ActivityManagerSelection& source) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=bap svc=6 stage=selection result=ok name=%.*s activity=%d "
                      "from_activity=%d reason=%d bubble=0x%X spawn=0x%X",
                      static_cast<int>(source.packageNameLength),
                      reinterpret_cast<const char*>(source.packageName.data()),
                      static_cast<int>(source.activityIndex),
                      static_cast<int>(source.sourceActivityIndex),
                      static_cast<int>(source.reason),
                      source.hasArrivalBubbleHash ? source.arrivalBubbleHash : 0U,
                      source.hasSpawnSetHash ? source.spawnSetHash : 0U);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool
prepare_allocation(const request_selection::ActivityManagerSelectionResult& parsed,
                   std::uint64_t& sessionId,
                   state::activity::PendingAllocation& allocation) noexcept {
    const bool hasCopy = parsed.hasSelection || parsed.hasSecondary;
    const request_selection::ActivityManagerSelection& source =
        hasCopy ? choose_copy(parsed) : parsed.selection;
    // an index-only selection takes the same State fallback as an absent one; refusing would leave svc 6 unanswered
    if (hasCopy && !source.hasPackageName) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=6 stage=package result=fallback");
    }
    if (!hasCopy || !source.hasPackageName) {
        return state::activity::prepare_session(sessionId, allocation);
    }
    report_selection(source);
    state::activity::destination::DestinationSelection destination{};
    destination.packageName = source.packageName;
    destination.packageNameLength = source.packageNameLength;
    destination.reason = source.reason;
    destination.previousActivityIndex = source.sourceActivityIndex;
    destination.activityIndex = source.activityIndex;
    destination.hasElementIndex = source.hasElementIndex;
    destination.elementIndex = source.hasElementIndex
                                   ? source.elementIndex
                                   : state::activity::destination::kAbsentElementIndex;
    destination.hasArrivalBubbleHash = source.hasArrivalBubbleHash;
    destination.arrivalBubbleHash = source.arrivalBubbleHash;
    destination.hasSpawnSetHash = source.hasSpawnSetHash;
    destination.spawnSetHash = source.spawnSetHash;
    destination.menuOptionCount=source.menuOptionCount;
    for (std::size_t i=0;i<source.menuOptionCount;++i)
        destination.menuOptions[i]={source.menuOptions[i].index,source.menuOptions[i].choice};
    if (source.descriptorBitLength != 0
        && source.descriptorBitLength <= destination.descriptorBits.size() * CHAR_BIT) {
        destination.descriptorBits = source.descriptorBits;
        destination.descriptorBitLength = static_cast<std::uint16_t>(source.descriptorBitLength);
        destination.descriptorNameBit = static_cast<std::uint16_t>(source.packageNameBitOffset);
        destination.hasDescriptorName = source.hasPackageName;
    }
    state::activity::defaults::ActivityDefaults defaults{};
    state::activity::defaults::snapshot(defaults);
    state::activity::defaults::apply_arrival_override(defaults, destination);
    return state::activity::prepare_session(destination, sessionId, allocation);
}

} // namespace

bool encode_response(std::span<const std::byte> requestBody,
                     std::span<std::byte> output,
                     std::size_t& written,
                     state::activity::PendingAllocation& allocation,
                     bool& hasAllocation) noexcept {
    written = 0;
    allocation = {};
    hasAllocation = false;
    middleware::bap::activity_host_manager::Request request;
    request_selection::ActivityManagerSelectionResult selection{};
    request_selection::StartupReservations startup{};
    // best effort: a malformed PUT must never cost the reply, and the client rejects any service-7 body that isn't exactly 137 bytes
    if (!middleware::bap::activity_host_manager::request::parse_request(requestBody, request)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=6 stage=request result=fallback");
    } else if (!request_selection::parse_selection(request.protobuf, selection)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=6 stage=selection result=fallback");
        selection = {};
    }
    if (!request_selection::parse_startup_reservations(request.protobuf, startup)) {
        startup={};
        core::log::write(core::log::Channel::server,core::log::Level::warn,
            "ev=activity stage=startup_reservations result=ignored reason=malformed");
    }
    std::uint64_t sessionId = state::activity::kAbsentSessionId;
    state::activity::PendingAllocation prepared{};
    if (!prepare_allocation(selection, sessionId, prepared)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=6 stage=allocation result=fail");
        return false;
    }
    // the startup response seeds the retail pending-key table from this list (4F6FAC..4F701B); those peers aren't re-requested via type13
    // commit their reservations with the allocation so join/identity snapshots include them
    const bool social=(selection.hasSelection || selection.hasSecondary)
        && state::activity::shared::social_destination(prepared.destination);
    // the sole owner is required too: its native identity establishes PRIVATE capacity before the first grant
    if (startup.present && (startup.members.count!=0 || social)) {
        std::array<state::activity::membership::Identity,32> peers{};
        for (std::size_t i=0;i<startup.members.count;++i) {
            const auto& p=startup.members.peers[i];
            peers[i]={p.memberKey,p.field1,p.field2,p.field3,p.accountSoid,p.field5,p.field6};
        }
        const auto nativePeers=std::span(startup.members.peers).first(startup.members.count);
        const bool authorized=social
            ? activity_message::membership::authorize_social_startup_reservations(nativePeers)
            : activity_message::membership::authorize_reservations(nativePeers);
        const bool staged=authorized && state::activity::prepare_startup_reservations(
            prepared,std::span(peers).first(startup.members.count));
        core::log::write(core::log::Channel::server,
            staged?core::log::Level::info:core::log::Level::warn,
            staged?"ev=activity stage=startup_reservations result=prepared"
                :authorized?"ev=activity stage=startup_reservations result=ignored reason=native_identity"
                           :"ev=activity stage=startup_reservations result=ignored reason=authorization");
        if(social && !staged) return false;
    }
    if(social && !state::activity::shared::prepare_social_allocation(prepared)) return false;
    if(social) core::log::write(core::log::Channel::server,core::log::Level::info,
        prepared.socialSessionId!=0?"ev=activity stage=social_primary result=prepared mode=reuse"
                                   :"ev=activity stage=social_primary result=prepared mode=create");
    if(prepared.socialSessionId!=0) sessionId=prepared.socialSessionId;
    else sessionId=prepared.sessionId;
    if (!middleware::bap::activity_host_manager::response::encode_response(
            sessionId, output, written)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=6 stage=encode result=fail");
        prepared = {};
        return false;
    }
    allocation = prepared;
    hasAllocation = true;
    return true;
}

} // namespace dawn::server::bap::encrypted::activity_host_manager
