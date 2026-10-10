#include "matchmaking_route.h"

#include <Windows.h>

#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/matchmaking/request/matchmaking_request_parser.h"
#include "../../../../middleware/bap/matchmaking/response/matchmaking_response_encoder.h"
#include "../../../../middleware/protobuf/codec.h"
#include "../../../../middleware/bap/matchmaking/social_search.h"
#include "../../../../middleware/bap/matchmaking/configuration_policy.h"
#include "../../../../state/matchmaking/social_search.h"
#include "../../../../state/runtime/runtime.h"
#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../RestorationServer/src/account_store.h"
#include "../../../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::bap::encrypted::matchmaking {
namespace {

namespace service = middleware::bap::matchmaking;

void bind_social_metadata(std::span<const std::byte> requestBody,
                           state::matchmaking::PendingMutation& mutation) noexcept {
#if defined(RESTORATION_DEDICATED_SERVER)
    const auto scope=service::social_search::scope(requestBody);
    const auto owner=state::account_snapshot().primarySoid;
    state::AccountState profile{};std::uint64_t platform{},revision{};
    std::array<char,::restoration::account_store::kAccountNameCapacity> name{};
    middleware::social::realtime::Lobby lobby{};
    if(!::restoration::account_store::profile_by_account(owner,platform,profile,revision,name))return;
    mutation.socialAccount=owner;mutation.socialPlatform=platform;
    mutation.socialActivity=scope.activity;
    mutation.socialPrimary=scope.valid && scope.primary && mutation.hasDescriptor
        && service::social_search::steam_descriptor({mutation.descriptorData,mutation.descriptorSize},platform)
        && ::restoration::realtime_store::lobby_for_member(platform,lobby)
        && lobby.ownerPlatformId==platform && lobby.memberCount<=lobby.members.size();
    if(!mutation.socialPrimary)return;
    for(std::size_t i=0;i<lobby.memberCount && i<mutation.socialParty.size();++i) {
        if(!::restoration::account_store::profile_by_platform(lobby.members[i].platformId,profile,revision,name)) {
            mutation.socialPrimary=false;mutation.socialParty={};return;
        }
        mutation.socialParty[i]=profile.primarySoid;
    }
#else
    (void)requestBody;(void)mutation;
#endif
}

static_assert(service::kJoinDescriptorSize == state::matchmaking::kDescriptorSize);

void append_scope(std::array<char, core::log::kLineCapacity>& line,
                  std::size_t& used,
                  const char* scope,
                  std::span<const std::byte> input) noexcept {
    middleware::protobuf::Reader reader(input);
    middleware::protobuf::Field field;
    while (reader.remaining() != 0 && reader.next(field) && used < line.size()) {
        const unsigned wire = static_cast<unsigned>(field.wireType);
        int appended = 0;
        if (field.wireType == middleware::protobuf::WireType::lengthDelimited) {
            appended = std::snprintf(line.data() + used,
                                     line.size() - used,
                                     " %sf%u/w%u/len%zu",
                                     scope,
                                     field.fieldNumber,
                                     wire,
                                     field.bytes.size());
        } else {
            appended = std::snprintf(line.data() + used,
                                     line.size() - used,
                                     " %sf%u/w%u/v0x%llX",
                                     scope,
                                     field.fieldNumber,
                                     wire,
                                     static_cast<unsigned long long>(field.value));
        }
        if (appended <= 0 || static_cast<std::size_t>(appended) >= line.size() - used) {
            used = line.size();
            return;
        }
        used += static_cast<std::size_t>(appended);
    }
}

// the opaque 128-byte join descriptor is never written to the log
void report_request_shape(std::span<const std::byte> input) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(
        line.data(), line.size(), "ev=matchmaking stage=request_shape bytes=%zu", input.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t used = static_cast<std::size_t>(prefix);
    append_scope(line, used, "r", input);

    middleware::protobuf::Reader root(input);
    middleware::protobuf::Field rootField;
    while (root.remaining() != 0 && root.next(rootField)) {
        if (rootField.fieldNumber != 8
            || rootField.wireType != middleware::protobuf::WireType::lengthDelimited) {
            continue;
        }
        append_scope(line, used, "a", rootField.bytes);
        middleware::protobuf::Reader advertisement(rootField.bytes);
        middleware::protobuf::Field advertisementField;
        while (advertisement.remaining() != 0 && advertisement.next(advertisementField)) {
            if (advertisementField.fieldNumber != 2
                || advertisementField.wireType
                       != middleware::protobuf::WireType::lengthDelimited) {
                continue;
            }
            append_scope(line, used, "d", advertisementField.bytes);
            break;
        }
        break;
    }
    if (used < line.size()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), used});
    }
}

void report_response_shape(const service::Request& request,
                           const service::Response& response,
                           bool encoded,
                           std::size_t written) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int length = std::snprintf(
        line.data(),
        line.size(),
        "ev=matchmaking stage=response_shape kind=%u has_request_nonce=%u request_nonce=0x%llX advertisement=0x%llX descriptor_bytes=%zu response_bytes=%zu result=%s",
        static_cast<unsigned>(request.kind),
        request.hasRequestNonce ? 1U : 0U,
        static_cast<unsigned long long>(request.requestNonce),
        static_cast<unsigned long long>(response.advertisementId),
        response.descriptor.size(),
        written,
        encoded ? "ok" : "encode");
    if (length > 0) {
        core::log::write(core::log::Channel::server,
                         encoded ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(length)});
    }
}

[[nodiscard]] bool prepare_fields(state::matchmaking::ContextHandle context,
                                  const service::Request& request,
                                  service::Response& response,
                                  state::matchmaking::PendingMutation& mutation) noexcept {
    response.kind = request.kind;
    switch (request.kind) {
    case service::RequestKind::advertisementUpdate:
        return state::matchmaking::prepare_variant_update(context,
                                                          request.advertisement.existingId,
                                                          request.advertisement.variantKey,
                                                          request.advertisement.hasDescriptor,
                                                          request.advertisement.descriptor,
                                                          response.advertisementId,
                                                          mutation);
    case service::RequestKind::rejoinAdvertisementUpdate:
        return state::matchmaking::prepare_initial_latest(
            context, response.advertisementId, mutation);
    case service::RequestKind::none:
    case service::RequestKind::sessionSearch:
    case service::RequestKind::advertisementDelete:
    case service::RequestKind::configuration:
    case service::RequestKind::rejoinAdvertisementDelete:
    case service::RequestKind::locateSession:
    case service::RequestKind::liveStats:
        return true;
    }
    return false;
}

} // namespace

bool encode_response(state::matchmaking::ContextHandle context,
                     std::span<const std::byte> requestBody,
                     std::span<std::byte> output,
                     std::size_t& written,
                     state::matchmaking::PendingMutation& mutation,
                     bool& hasMutation) noexcept {
    written = 0;
    hasMutation = false;
    SecureZeroMemory(&mutation, sizeof mutation);
    report_request_shape(requestBody);
    const service::Request request = service::request::parse(requestBody);
    service::Response response{};
    if (!prepare_fields(context, request, response, mutation)) {
        SecureZeroMemory(&mutation, sizeof mutation);
        response = {};
    }

    state::matchmaking::LatestSnapshot latest{};
    if (response.kind == service::RequestKind::configuration)
        response.towerConfiguration = service::configuration_policy::tower(requestBody);
    if(response.kind==service::RequestKind::advertisementUpdate)
        bind_social_metadata(requestBody,mutation);
    if(response.kind==service::RequestKind::sessionSearch) {
        const auto scope=service::social_search::scope(requestBody);
        if(scope.valid && scope.primary && state::matchmaking::prepare_social_search(
            context,scope.activity,latest,mutation)) {
            response.advertisementId=latest.advertisementId;response.descriptor=latest.descriptor;
        }
    }
    if (response.kind == service::RequestKind::locateSession
        && state::matchmaking::latest_snapshot(context, latest)) {
        response.advertisementId = latest.advertisementId;
        if (latest.hasDescriptor) {
            response.descriptor = std::span(latest.descriptor);
        }
    }
    const bool encoded = service::response::encode(response, output, written);
    report_response_shape(request, response, encoded, written);
    state::matchmaking::erase_snapshot(latest);
    if (!encoded) {
        written = 0;
        SecureZeroMemory(&mutation, sizeof mutation);
        return false;
    }
    hasMutation = mutation.kind != state::matchmaking::MutationKind::none;
    return true;
}

} // namespace dawn::server::bap::encrypted::matchmaking
