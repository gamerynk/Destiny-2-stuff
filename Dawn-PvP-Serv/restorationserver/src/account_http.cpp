#include "../../dawn/src/middleware/bap/native_certificate_publication.h"
#include "account_http.h"
#include "server/transport/native_certificate_cache.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <limits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "account_codec.h"
#include "account_store.h"
#include "expansions.h"
#include "platform_api/platform_api.h"
#include "roles.h"
#include "core/identity/device_identity.h"
#include "core/identity/local_ticket.h"
#include "core/logging/log.h"
#include "middleware/signon/response.h"
#include "middleware/content/bootstrap_configuration.h"
#include "server/http/content_configuration.h"
#include "middleware/social/friend_snapshot_codec.h"
#include "middleware/mission/observation_wire.h"
#include "middleware/social/realtime_codec.h"
#include "realtime_store.h"
#include "state/entitlements/entitlement_runtime.h"
#include "state/activity/mission_host/mission_observations.h"
#include "state/activity/crucible/match_runtime.h"
#include "middleware/crucible/match_view_wire.h"
#include "state/runtime/runtime.h"
#include "state/session/session_runtime.h"
#include "state/social/social_state.h"
#include "steam/runtime/runtime.h"

namespace restoration::account_http {
namespace {

constexpr std::string_view kLinkCodePath = "/v1/account/discord-link-code";
constexpr std::string_view kSignOnPath = "/SignOn";
constexpr std::string_view kFriendsPath = "/v1/social/friends";
constexpr std::string_view kRealtimePath = "/v1/social/realtime";
constexpr std::string_view kProfilePath = "/v1/social/profile";
constexpr std::string_view kRelationshipsPath = "/v1/social/relationships";
constexpr std::string_view kMessagesPath = "/v1/social/messages";
constexpr std::string_view kAvatarsPath = "/v1/social/avatars";
constexpr std::string_view kDiscordBotPath = "/v1/bot/discord";
constexpr std::string_view kDiscordStatusPath = "/v1/bot/status";
constexpr std::string_view kBotPendingPath = "/v1/bot/pending";
constexpr std::string_view kDiscordLinkStatePath = "/v1/account/discord-link-state";
constexpr std::string_view kRenamePath = "/v1/account/name";
constexpr std::string_view kDiscordUnlinkPath = "/v1/account/discord-unlink";
constexpr std::string_view kCharactersPath = "/v1/account/characters";
constexpr std::string_view kCharacterDeletePath = "/v1/account/character/delete";
constexpr std::string_view kCharacterCreatePath = "/v1/account/character/create";
constexpr std::string_view kOwnCharactersPath = "/v1/account/characters/mine";
constexpr std::string_view kCharacterCustomizePath = "/v1/account/character/customize";
constexpr std::string_view kExpansionsPath = "/v1/account/expansions/steam";
constexpr std::string_view kExpansionsResultPath = "/v1/account/expansions/steam/result";
constexpr std::string_view kJobsResultPath = "/v1/account/jobs/result";
constexpr std::string_view kLauncherProfilePath = "/v1/account/launcher/profile";
constexpr std::string_view kCodesIssuePath = "/v1/account/codes/issue";
constexpr std::string_view kCodesRedeemPath = "/v1/account/codes/redeem";
constexpr std::string_view kCodesForgetPath = "/v1/account/codes/forget";
constexpr std::string_view kBotRolesPath = "/v1/bot/roles";
constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpForbidden = 403;
constexpr unsigned kHttpUnavailable = 503;
constexpr std::size_t kMessageCapacity = 4096;

void write_realtime_diagnostic(realtime_store::DiagnosticLevel level,
                               std::string_view event) noexcept {
    dawn::core::log::write(
        dawn::core::log::Channel::server,
        level == realtime_store::DiagnosticLevel::warning
            ? dawn::core::log::Level::warn
            : dawn::core::log::Level::info,
        event);
}

[[nodiscard]] std::uint64_t now_seconds() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

[[nodiscard]] bool route_matches(std::string_view url, std::string_view path) noexcept {
    return url == path
           || (url.size() > path.size() && url.starts_with(path)
               && url[path.size()] == '?');
}

[[nodiscard]] bool authenticate_device(
    dawn::core::identity::device_identity::Scope scope,
    std::span<const std::byte> proof,
    std::uint64_t now,
    account_store::DeviceBinding& binding) noexcept {
    dawn::core::identity::device_identity::Claims claims{};
    return dawn::core::identity::device_identity::verify(scope, proof, now, claims)
           && account_store::load_or_create_device(
               claims.deviceId, claims.platformId, claims.publicKey, binding)
           && binding.platformId == claims.platformId && binding.accountSoid != 0;
}

[[nodiscard]] bool authenticate_social_identity(
    dawn::core::identity::device_identity::Scope scope,
    std::span<const std::byte> body,
    std::uint64_t now,
    account_store::DeviceBinding& binding,
    std::size_t& proofSize) noexcept {
    binding = {};
    proofSize = 0;
    if (body.size() >= dawn::core::identity::device_identity::kProofSize
        && authenticate_device(scope, body, now, binding)) {
        proofSize = dawn::core::identity::device_identity::kProofSize;
        return true;
    }
    dawn::core::identity::local_ticket::Claims identity{};
    const dawn::state::AccountState owner = dawn::state::account_snapshot();
    if (body.size() < dawn::core::identity::local_ticket::kTicketSize
        || !dawn::core::identity::local_ticket::authenticate(body, now, identity)
        || identity.provider != dawn::core::identity::local_ticket::kProviderSteam
        || identity.platformId != dawn::steam::local_steam_id()
        || owner.primarySoid == 0) {
        return false;
    }
    binding.accountSoid = owner.primarySoid;
    binding.platformId = identity.platformId;
    proofSize = dawn::core::identity::local_ticket::kTicketSize;
    return true;
}

[[nodiscard]] bool copy_response(std::string_view body,
                                 unsigned status,
                                 const dawn::client::network::HttpRequest& request,
                                 dawn::client::network::HttpResponse& response) noexcept {
    if (body.size() > request.response.size()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        response.contentType = "text/plain; charset=utf-8";
        return true;
    }
    std::copy(body.begin(), body.end(), reinterpret_cast<char*>(request.response.data()));
    response.statusCode = status;
    response.size = body.size();
    response.contentType = "text/plain; charset=utf-8";
    return true;
}

[[nodiscard]] bool printable_field(std::string_view value,
                                   std::size_t maximum) noexcept {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](const char character) noexcept {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20U && byte <= 0x7EU;
    });
}

[[nodiscard]] bool parse_bot_request(std::span<const std::byte> body,
                                     std::string_view& command,
                                     std::string_view& discordUserId,
                                     std::string_view& argument) noexcept {
    if (body.empty() || body.size() > 1024) {
        return false;
    }
    std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
    if (!text.empty() && text.back() == '\n') {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.back() == '\r') {
        text.remove_suffix(1);
    }
    const std::size_t first = text.find('\n');
    const std::size_t second = first == std::string_view::npos
                                   ? std::string_view::npos
                                   : text.find('\n', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos
        || text.find('\n', second + 1) != std::string_view::npos) {
        return false;
    }
    command = text.substr(0, first);
    discordUserId = text.substr(first + 1, second - first - 1);
    argument = text.substr(second + 1);
    if (!command.empty() && command.back() == '\r') {
        command.remove_suffix(1);
    }
    if (!discordUserId.empty() && discordUserId.back() == '\r') {
        discordUserId.remove_suffix(1);
    }
    if (!argument.empty() && argument.back() == '\r') {
        argument.remove_suffix(1);
    }
    return printable_field(command, 32) && printable_field(discordUserId, 32)
           && (argument.empty() || printable_field(argument, 128));
}

[[nodiscard]] bool refresh_social_projection() noexcept {
    dawn::state::social::Snapshot snapshot{};
    return account_store::social_snapshot(dawn::steam::local_steam_id(), snapshot)
           && dawn::state::social::publish(snapshot);
}

[[nodiscard]] std::uint64_t account_link_platform(
    const dawn::client::network::HttpRequest& request,
    std::uint64_t now,
    account_store::DeviceBinding& binding) noexcept {
    if (request.body.size() == dawn::core::identity::device_identity::kProofSize) {
        return authenticate_device(
                   dawn::core::identity::device_identity::Scope::discordLinkCode,
                   request.body,
                   now,
                   binding)
                   ? binding.platformId
                   : 0;
    }
    dawn::core::identity::local_ticket::Claims identity{};
    if (dawn::core::identity::local_ticket::authenticate(request.body, now, identity)
        && identity.provider == dawn::core::identity::local_ticket::kProviderSteam
        && identity.platformId == dawn::steam::local_steam_id()) {
        return identity.platformId;
    }
    return 0;
}

[[nodiscard]] bool handle_link_code(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t now = now_seconds();
    std::uint64_t platformId = 0;
    account_store::DeviceBinding binding{};
    if (request.body.size()
        == dawn::core::identity::device_identity::kProofSize) {
        if (authenticate_device(
                dawn::core::identity::device_identity::Scope::discordLinkCode,
                request.body,
                now,
                binding)) {
            platformId = binding.platformId;
        }
    } else {
        dawn::core::identity::local_ticket::Claims identity{};
        if (dawn::core::identity::local_ticket::authenticate(request.body, now, identity)
            && identity.provider
                   == dawn::core::identity::local_ticket::kProviderSteam
            && identity.platformId == dawn::steam::local_steam_id()) {
            platformId = identity.platformId;
        }
    }
    if (now == 0 || platformId == 0) {
        dawn::core::log::write(dawn::core::log::Channel::server,
                                  dawn::core::log::Level::warn,
                                  "ev=discord_link stage=issue result=fail reason=identity_ticket");
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    }

    account_store::DiscordLinkCode link{};
    if (!account_store::issue_discord_link_code(platformId, now, link)) {
        return copy_response("Unable to create a link code.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    std::array<char, 192> output{};
    const int count = std::snprintf(output.data(),
                                    output.size(),
                                    "%s\n%s\n%llu",
                                    link.code.data(),
                                    link.accountName.data(),
                                    static_cast<unsigned long long>(link.expiresAt));
    if (count < 0 || static_cast<std::size_t>(count) >= output.size()) {
        return copy_response("Unable to create a link code.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    dawn::core::log::write(dawn::core::log::Channel::server,
                              dawn::core::log::Level::info,
                              "ev=discord_link stage=issue result=ok");
    return copy_response(std::string_view(output.data(), static_cast<std::size_t>(count)),
                         kHttpOk,
                         request,
                         response);
}

[[nodiscard]] bool handle_link_state(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    const std::uint64_t platformId =
        now != 0 ? account_link_platform(request, now, binding) : 0;
    if (platformId == 0) {
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    }
    account_store::DiscordLinkState state{};
    if (!account_store::discord_link_state(platformId, state)) {
        return copy_response("Unable to read the Discord link state.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    std::array<char, 192> output{};
    const int count =
        std::snprintf(output.data(), output.size(), "%s\n%s\n0",
                      state.linked ? "linked" : "unlinked", state.accountName.data());
    if (count < 0 || static_cast<std::size_t>(count) >= output.size()) {
        return copy_response("Unable to read the Discord link state.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    return copy_response(std::string_view(output.data(), static_cast<std::size_t>(count)),
                         kHttpOk,
                         request,
                         response);
}

[[nodiscard]] bool handle_unlink_request(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    const std::uint64_t platformId =
        now != 0 ? account_link_platform(request, now, binding) : 0;
    if (platformId == 0) {
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    }
    account_store::DiscordLinkState state{};
    if (!account_store::request_discord_unlink(platformId, now, state)) {
        return copy_response("This account is not linked to Discord.",
                             kHttpBadRequest,
                             request,
                             response);
    }
    std::array<char, 192> output{};
    const int count = std::snprintf(output.data(), output.size(), "%s\n%s\n0", "pending",
                                    state.accountName.data());
    if (count < 0 || static_cast<std::size_t>(count) >= output.size()) {
        return copy_response("Unable to start the unlink.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    dawn::core::log::write(dawn::core::log::Channel::server,
                              dawn::core::log::Level::info,
                              "ev=discord_link stage=unlink_request result=pending");
    return copy_response(std::string_view(output.data(), static_cast<std::size_t>(count)),
                         kHttpOk,
                         request,
                         response);
}

[[nodiscard]] bool handle_device_sign_on(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    // retail wraps the app-ticket bytes in its SignOn payload and the device verifier scans a
    // bounded span for the proof, so do not require the proof to be the whole body
    if (request.body.size()
        < dawn::core::identity::device_identity::kProofSize) {
        return false;
    }
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    dawn::core::identity::device_identity::Claims claims{};
    const bool proofValid =
        now != 0
        && dawn::core::identity::device_identity::verify(
            dawn::core::identity::device_identity::Scope::signOn, request.body, now, claims);
    if (!proofValid
        || !authenticate_device(dawn::core::identity::device_identity::Scope::signOn,
                                 request.body,
                                 now,
                                 binding)) {
        // larger legacy SignOn payloads go to the owner-ticket route, which logs only identity_ticket,
        // so name the device-side cause here first
        std::array<char, 160> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=http route=signon stage=device result=fail reason=%s bytes=%zu",
            proofValid ? "account" : "proof",
            request.body.size());
        if (count > 0) {
            dawn::core::log::write(dawn::core::log::Channel::server,
                                      dawn::core::log::Level::warn,
                                      {line.data(), static_cast<std::size_t>(count)});
        }
        return false;
    }
    if (!dawn::state::bootstrap_token_present()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    dawn::state::session::Credentials credentials{};
    dawn::server::http::content_configuration::Snapshot content{};
    const dawn::state::entitlements::Table entitlements =
        restoration::expansions::signon_table(binding.platformId);
    if (!dawn::server::http::content_configuration::snapshot(claims.deviceId, binding.platformId, content)
        || !dawn::state::session::issue(binding.accountSoid,
                                        binding.platformId,
                                        dawn::state::sign_on(),
                                        now,
                                        credentials)
        || !dawn::middleware::signon::encode_success(
            credentials.signOn,
            entitlements,
            credentials.expiresAt,
            now,
            request.response,
            response.size,
            {content.url.data(), content.guid.data()})) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    if (!realtime_store::begin_game_session(binding.platformId,
                                            binding.accountSoid,
                                            credentials.generation,
                                            now)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        dawn::core::log::write(
            dawn::core::log::Channel::server,
            dawn::core::log::Level::warn,
            "ev=http route=signon result=fail reason=realtime_generation_reset");
        return true;
    }
    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    dawn::core::log::write(dawn::core::log::Channel::server,
                              dawn::core::log::Level::info,
                              binding.created
                                  ? "ev=http route=signon result=ok account=device_new"
                                  : "ev=http route=signon result=ok account=device_existing");
    return true;
}

[[nodiscard]] bool handle_social(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    std::size_t proofSize = 0;
    if (now == 0
        || !authenticate_social_identity(
            dawn::core::identity::device_identity::Scope::socialSnapshot,
            request.body,
            now,
            binding,
            proofSize)
        || request.body.size() != proofSize) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }
    dawn::state::social::Snapshot snapshot{};
    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    if (!account_store::social_snapshot(binding.platformId, snapshot)
        || !dawn::middleware::social::friend_snapshot::encode(
            snapshot, request.response, response.size)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
    }
    return true;
}

[[nodiscard]] bool handle_device_realtime(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    namespace realtime = dawn::middleware::social::realtime;
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    std::size_t proofSize = 0;
    if (now == 0
        || !authenticate_social_identity(
            dawn::core::identity::device_identity::Scope::socialRealtime,
            request.body,
            now,
            binding,
            proofSize)
        || request.body.size() <= proofSize) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }
    realtime::Request decoded{};
    const auto operation = request.body.subspan(proofSize);
    if (!realtime::decode_request(operation, decoded)) {
        response.statusCode = kHttpBadRequest;
        response.size = 0;
        return true;
    }
    realtime::Response result{};
    if (!realtime_store::execute(binding.platformId,
                                 binding.accountSoid,
                                 now,
                                  decoded,
                                  &account_store::accepted_friend,
                                  result,
                                  &write_realtime_diagnostic)
        || !realtime::encode_response(result, request.response, response.size)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    return true;
}

[[nodiscard]] bool handle_mission_observe(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    namespace wire = dawn::middleware::mission::observation_wire;
    namespace observations = dawn::state::activity::mission_observations;
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    std::size_t proofSize = 0;
    if (now == 0
        || !authenticate_social_identity(
            dawn::core::identity::device_identity::Scope::missionObservation,
            request.body,
            now,
            binding,
            proofSize)
        || request.body.size() <= proofSize) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }
    wire::Request decoded{};
    if (!wire::decode(request.body.subspan(proofSize), decoded)) {
        response.statusCode = kHttpBadRequest;
        response.size = 0;
        return true;
    }
    wire::Response result{};
    // a client outside the served fireteam gets an empty answer so its hooks stay idle
    static_cast<void>(observations::apply(binding.platformId, decoded, result));
    if (!wire::encode(result, request.response, response.size)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    return true;
}

[[nodiscard]] bool handle_device_profile(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    constexpr std::size_t kTargetSize = sizeof(std::uint64_t);
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    std::size_t proofSize = 0;
    if (now == 0
        || !authenticate_social_identity(
            dawn::core::identity::device_identity::Scope::socialProfile,
            request.body,
            now,
            binding,
            proofSize)
        || request.body.size() != proofSize + kTargetSize) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }
    std::uint64_t targetPlatformId = 0;
    std::memcpy(&targetPlatformId,
                request.body.data() + proofSize,
                sizeof targetPlatformId);
    dawn::state::AccountState profile{};
    std::uint64_t revision = 0;
    std::vector<std::byte> encoded;
    if (!account_store::friend_profile(
            binding.platformId, targetPlatformId, profile, revision)
        // clients decode this with their bundled v1 codec
        || !account_codec::encode(profile, encoded, account_codec::kPriorVersion)) {
        response.statusCode = kHttpForbidden;
        response.size = 0;
        return true;
    }
    if (sizeof revision > request.response.size()
        || encoded.size() > request.response.size() - sizeof revision) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    std::memcpy(request.response.data(), &revision, sizeof revision);
    std::copy(encoded.begin(),
              encoded.end(),
              request.response.begin() + static_cast<std::ptrdiff_t>(sizeof revision));
    response.statusCode = kHttpOk;
    response.size = sizeof revision + encoded.size();
    response.contentType = "application/octet-stream";
    return true;
}

[[nodiscard]] bool handle_account_rename(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    namespace identity = dawn::core::identity::device_identity;
    constexpr auto proofSize = identity::kProofSize;
    const auto now = now_seconds();
    account_store::DeviceBinding binding{};
    if (request.body.size() < proofSize
        || !authenticate_device(identity::Scope::accountRename,
                                request.body.first(proofSize), now, binding)) {
        return copy_response("Sign in to Dawn first.", kHttpUnauthorized, request, response);
    }
    if (request.body.size() <= proofSize + 1) {
        return copy_response("Enter a name.", kHttpBadRequest, request, response);
    }
    const auto length = std::to_integer<std::size_t>(request.body[proofSize]);
    if (length < 3 || length >= account_store::kAccountNameCapacity
        || request.body.size() != proofSize + 1 + length) {
        return copy_response("Names must be 3-63 letters, numbers, spaces, _ or -.",
                             kHttpBadRequest, request, response);
    }
    const std::string_view name(
        reinterpret_cast<const char*>(request.body.data() + proofSize + 1), length);
    std::array<char, 256> output{};
    std::size_t written{};
    const bool success = account_store::rename_account(
        binding.platformId, name, now, output, written);
    if (success && !refresh_social_projection()) {
        return copy_response("Name saved. Restart Dawn to refresh it.",
                             kHttpOk, request, response);
    }
    return copy_response({output.data(), written}, success ? kHttpOk : kHttpBadRequest,
                         request, response);
}

// the actor comes from the proof, never from the request body
[[nodiscard]] bool handle_relationships(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    constexpr std::size_t kProof = dawn::core::identity::device_identity::kProofSize;
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    if (now == 0 || request.body.size() < kProof + 2
        || !authenticate_device(
            dawn::core::identity::device_identity::Scope::socialRelationships,
            request.body.subspan(0, kProof),
            now,
            binding)) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }

    const auto actionByte = static_cast<std::uint8_t>(request.body[kProof]);
    const auto nameSize = static_cast<std::size_t>(request.body[kProof + 1]);
    if (actionByte < 1 || actionByte > 4 || nameSize == 0
        || request.body.size() != kProof + 2 + nameSize
        || nameSize >= account_store::kAccountNameCapacity) {
        return copy_response("Malformed request.", kHttpBadRequest, request, response);
    }
    const std::string_view name(
        reinterpret_cast<const char*>(request.body.data() + kProof + 2), nameSize);

    std::array<char, kMessageCapacity> output{};
    std::size_t written = 0;
    const bool success = account_store::execute_relationship(
        binding.platformId,
        static_cast<account_store::RelationshipAction>(actionByte),
        name,
        now,
        output,
        written);

    // the in-game roster comes from a projection, so rebuild it before the player alt-tabs back
    if (success && !refresh_social_projection()) {
        return copy_response("Saved, but the live roster could not refresh.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    const std::string_view body = written == 0
                                      ? std::string_view(success ? "Done." : "That did not work.")
                                      : std::string_view(output.data(), written);
    return copy_response(body, success ? kHttpOk : kHttpBadRequest, request, response);
}

[[nodiscard]] bool handle_messages(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    constexpr std::size_t kProof = dawn::core::identity::device_identity::kProofSize;
    constexpr std::uint8_t kPoll = 1;
    constexpr std::uint8_t kSend = 2;
    constexpr std::size_t kHeader = 24;
    constexpr std::size_t kRecord = 8 + 8 + 8 + 8 + account_store::kAccountNameCapacity
                                    + account_store::kDirectMessageCapacity;

    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    if (now == 0 || request.body.size() < kProof + 1
        || !authenticate_device(
            dawn::core::identity::device_identity::Scope::socialMessages,
            request.body.subspan(0, kProof),
            now,
            binding)) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }

    const auto operation = static_cast<std::uint8_t>(request.body[kProof]);
    if (operation == kSend) {
        if (request.body.size() < kProof + 4) {
            return copy_response("Malformed request.", kHttpBadRequest, request, response);
        }
        const auto nameSize = static_cast<std::size_t>(request.body[kProof + 1]);
        const auto bodySize = static_cast<std::size_t>(request.body[kProof + 2])
                              | (static_cast<std::size_t>(request.body[kProof + 3]) << 8U);
        if (nameSize == 0 || nameSize >= account_store::kAccountNameCapacity || bodySize == 0
            || bodySize > account_store::kDirectMessageCapacity
            || request.body.size() != kProof + 4 + nameSize + bodySize) {
            return copy_response("Malformed request.", kHttpBadRequest, request, response);
        }
        const auto* const cursor = reinterpret_cast<const char*>(request.body.data()) + kProof + 4;
        std::array<char, kMessageCapacity> output{};
        std::size_t written = 0;
        const bool success = account_store::send_direct_message(
            binding.platformId,
            std::string_view(cursor, nameSize),
            std::string_view(cursor + nameSize, bodySize),
            now,
            output,
            written);
        const std::string_view reply =
            written == 0 ? std::string_view(success ? "Sent." : "That did not work.")
                         : std::string_view(output.data(), written);
        return copy_response(reply, success ? kHttpOk : kHttpBadRequest, request, response);
    }

    if (operation != kPoll || request.body.size() != kProof + 1 + sizeof(std::uint64_t)) {
        return copy_response("Malformed request.", kHttpBadRequest, request, response);
    }
    std::uint64_t cursor = 0;
    std::memcpy(&cursor, request.body.data() + kProof + 1, sizeof cursor);

    std::array<account_store::DirectMessage, account_store::kDirectMessagePage> page{};
    std::size_t count = 0;
    std::uint64_t nextCursor = cursor;
    if (!account_store::poll_direct_messages(binding.platformId, cursor, page, count, nextCursor)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    if (request.response.size() < kHeader + count * kRecord) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }

    auto* const out = reinterpret_cast<std::uint8_t*>(request.response.data());
    std::memset(out, 0, kHeader + count * kRecord);
    constexpr std::uint32_t kMagic = 0x3153'4D44; // "DMS1"
    const auto version = static_cast<std::uint16_t>(1);
    const auto rows = static_cast<std::uint16_t>(count);
    std::memcpy(out, &kMagic, sizeof kMagic);
    std::memcpy(out + 4, &version, sizeof version);
    std::memcpy(out + 6, &rows, sizeof rows);
    std::memcpy(out + 8, &nextCursor, sizeof nextCursor);

    for (std::size_t index = 0; index < count; ++index) {
        const account_store::DirectMessage& row = page[index];
        std::uint8_t* const record = out + kHeader + index * kRecord;
        std::memcpy(record, &row.id, sizeof row.id);
        std::memcpy(record + 8, &row.sentAt, sizeof row.sentAt);
        std::memcpy(record + 16, &row.peerPlatformId, sizeof row.peerPlatformId);
        record[24] = row.outgoing ? 1U : 0U;
        std::memcpy(record + 32, row.peerName.data(), row.peerName.size());
        std::memcpy(record + 32 + row.peerName.size(), row.body.data(), row.body.size());
    }

    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    response.size = kHeader + count * kRecord;
    return true;
}

[[nodiscard]] bool handle_avatars(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    constexpr std::size_t kProof = dawn::core::identity::device_identity::kProofSize;
    constexpr std::size_t kHeader = 8;
    constexpr std::size_t kRecord = 8 + account_store::kAvatarCapacity;
    constexpr std::size_t kMaxRows = 64;

    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    if (now == 0 || request.body.size() < kProof + 1
        || !authenticate_device(
            dawn::core::identity::device_identity::Scope::socialAvatars,
            request.body.subspan(0, kProof),
            now,
            binding)) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        return true;
    }
    const auto selfSize = static_cast<std::size_t>(request.body[kProof]);
    if (selfSize >= account_store::kAvatarCapacity
        || request.body.size() != kProof + 1 + selfSize) {
        return copy_response("Malformed request.", kHttpBadRequest, request, response);
    }
    const std::string_view own(
        reinterpret_cast<const char*>(request.body.data()) + kProof + 1, selfSize);

    std::array<account_store::FriendAvatar, kMaxRows> rows{};
    std::size_t count = 0;
    if (!account_store::exchange_avatars(binding.platformId, own, now, rows, count)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    if (request.response.size() < kHeader + count * kRecord) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }

    auto* const out = reinterpret_cast<std::uint8_t*>(request.response.data());
    std::memset(out, 0, kHeader + count * kRecord);
    constexpr std::uint32_t kMagic = 0x3154'5641; // "AVT1"
    const auto version = static_cast<std::uint16_t>(1);
    const auto written = static_cast<std::uint16_t>(count);
    std::memcpy(out, &kMagic, sizeof kMagic);
    std::memcpy(out + 4, &version, sizeof version);
    std::memcpy(out + 6, &written, sizeof written);
    for (std::size_t index = 0; index < count; ++index) {
        std::uint8_t* const record = out + kHeader + index * kRecord;
        std::memcpy(record, &rows[index].platformId, sizeof rows[index].platformId);
        std::memcpy(record + 8, rows[index].avatar.data(), rows[index].avatar.size());
    }
    response.statusCode = kHttpOk;
    response.contentType = "application/octet-stream";
    response.size = kHeader + count * kRecord;
    return true;
}

[[nodiscard]] bool handle_discord_bot(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    std::string_view command;
    std::string_view discordUserId;
    std::string_view argument;
    if (!parse_bot_request(request.body, command, discordUserId, argument)) {
        return copy_response("Malformed command.", kHttpBadRequest, request, response);
    }

    std::array<char, kMessageCapacity> output{};
    std::size_t written = 0;
    const std::uint64_t now = now_seconds();
    bool success = false;
    bool socialMutation = false;
    if (command == "ACCOUNT_LINK") {
        success = account_store::redeem_discord_link(
            discordUserId, argument, now, output, written);
    } else if (command == "ACCOUNT_UNLINK_CONFIRM"
               || command == "ACCOUNT_UNLINK_CANCEL") {
        success = account_store::execute_discord_unlink(
            discordUserId,
            command == "ACCOUNT_UNLINK_CONFIRM",
            now,
            output,
            written);
    } else if (command == "ROLES_SET" || command == "ROLES_PRUNE") {
        const bool digits = !argument.empty() && argument.size() <= 12
                            && std::all_of(argument.begin(), argument.end(), [](char character) noexcept {
                                   return character >= '0' && character <= '9';
                               });
        std::uint64_t value = 0;
        for (const char character : digits ? argument : std::string_view{}) {
            value = value * 10 + static_cast<std::uint64_t>(character - '0');
        }
        if (command == "ROLES_SET") {
            if (!digits || value >= roles::kMaskLimit) {
                return copy_response("Invalid role set.", kHttpBadRequest, request, response);
            }
            success = account_store::set_discord_roles(discordUserId, static_cast<std::uint32_t>(value), now);
        } else {
            if (!digits || value == 0 || value > now) {
                return copy_response("Invalid prune time.", kHttpBadRequest, request, response);
            }
            success = account_store::prune_discord_roles(value);
        }
    } else {
        account_store::DiscordAction action{};
        if (command == "ACCOUNT_RENAME") {
            action = account_store::DiscordAction::accountRename;
            socialMutation = true;
        } else if (command == "FRIEND_ADD") {
            action = account_store::DiscordAction::friendAdd;
            socialMutation = true;
        } else if (command == "FRIEND_ACCEPT") {
            action = account_store::DiscordAction::friendAccept;
            socialMutation = true;
        } else if (command == "FRIEND_DECLINE") {
            action = account_store::DiscordAction::friendDecline;
            socialMutation = true;
        } else if (command == "FRIEND_REMOVE") {
            action = account_store::DiscordAction::friendRemove;
            socialMutation = true;
        } else if (command == "FRIEND_LIST") {
            action = account_store::DiscordAction::friendList;
        } else {
            return copy_response("Unknown command.", kHttpBadRequest, request, response);
        }
        success = account_store::execute_discord_action(
            discordUserId, action, argument, now, output, written);
    }

    if (success && socialMutation && !refresh_social_projection()) {
        return copy_response("Relationship saved, but the live roster could not refresh.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    if (written == 0) {
        const std::string_view fallback = success ? "Done." : "Command failed.";
        return copy_response(fallback,
                             success ? kHttpOk : kHttpBadRequest,
                             request,
                             response);
    }
    return copy_response(std::string_view(output.data(), written),
                         success ? kHttpOk : kHttpBadRequest,
                         request,
                         response);
}

void append_json_string(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character >= 0x20U) output.push_back(static_cast<char>(character));
            break;
        }
    }
    output.push_back('"');
}

[[nodiscard]] bool handle_bot_pending(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    std::array<account_store::PendingUnlink, 16> pending{};
    std::size_t count = 0;
    if (!account_store::claim_discord_unlinks(pending, count)) {
        return copy_response("Unable to read pending unlinks.",
                             kHttpUnavailable,
                             request,
                             response);
    }
    std::string body;
    body.reserve(2048);
    body.push_back('[');
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            body.push_back(',');
        }
        body += "{\"discordUserId\":";
        append_json_string(body, pending[index].discordUserId.data());
        body += ",\"accountName\":";
        append_json_string(body, pending[index].accountName.data());
        body.push_back('}');
    }
    body.push_back(']');
    if (body.size() > request.response.size()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    std::copy(body.begin(), body.end(), reinterpret_cast<char*>(request.response.data()));
    response.statusCode = kHttpOk;
    response.size = body.size();
    response.contentType = "application/json; charset=utf-8";
    return true;
}

[[nodiscard]] bool handle_discord_status(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    realtime_store::StatusSnapshot snapshot{};
    if (!realtime_store::status_snapshot(now_seconds(), snapshot)) {
        return copy_response("Unable to read live status.", kHttpUnavailable, request, response);
    }
    std::array<std::array<char, account_store::kAccountNameCapacity>, 128> names{};
    for (std::size_t index = 0; index < snapshot.playerCount; ++index) {
        dawn::state::AccountState account{};
        std::uint64_t platformId = 0;
        std::uint64_t revision = 0;
        if (!account_store::profile_by_account(snapshot.players[index].accountSoid,
                                               platformId, account, revision, names[index])) {
            std::snprintf(names[index].data(), names[index].size(), "Guardian");
        }
    }
    std::string body;
    body.reserve(4096);
    body += "{\"onlineCount\":" + std::to_string(snapshot.playerCount) + ",\"players\":[";
    for (std::size_t index = 0; index < snapshot.playerCount; ++index) {
        if (index != 0) body.push_back(',');
        const auto& player = snapshot.players[index];
        body.push_back('{');
        body += "\"name\":";
        append_json_string(body, names[index].data());
        const std::string_view activity = player.status[0] != '\0'
            ? std::string_view(player.status.data())
            : (player.steamDisplay[0] != '\0'
                ? std::string_view(player.steamDisplay.data()) : "In game");
        body += ",\"activity\":";
        append_json_string(body, activity);
        std::size_t visibleTeamSize = 0;
        for (std::size_t member = 0; member < player.teamCount; ++member) {
            for (std::size_t candidate = 0; candidate < snapshot.playerCount; ++candidate) {
                if (snapshot.players[candidate].platformId == player.teamPlatformIds[member]) {
                    ++visibleTeamSize;
                    break;
                }
            }
        }
        body += ",\"teamSize\":" + std::to_string(visibleTeamSize) + ",\"teamMembers\":[";
        bool firstMember = true;
        for (std::size_t member = 0; member < player.teamCount; ++member) {
            for (std::size_t candidate = 0; candidate < snapshot.playerCount; ++candidate) {
                if (snapshot.players[candidate].platformId == player.teamPlatformIds[member]) {
                    if (!firstMember) body.push_back(',');
                    append_json_string(body, names[candidate].data());
                    firstMember = false;
                    break;
                }
            }
        }
        body += "]}";
    }
    body += "]}";
    if (body.size() > request.response.size()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    std::copy(body.begin(), body.end(), reinterpret_cast<char*>(request.response.data()));
    response.statusCode = kHttpOk;
    response.size = body.size();
    response.contentType = "application/json; charset=utf-8";
    return true;
}

[[nodiscard]] std::size_t parse_numbers(std::span<const std::byte> body,
                                        std::span<unsigned> output) noexcept {
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < body.size() && count < output.size()) {
        const unsigned char current = std::to_integer<unsigned char>(body[index]);
        if (current < '0' || current > '9') {
            ++index;
            continue;
        }
        unsigned value = 0;
        while (index < body.size()) {
            const unsigned char digit = std::to_integer<unsigned char>(body[index]);
            if (digit < '0' || digit > '9') {
                break;
            }
            value = value * 10U + static_cast<unsigned>(digit - '0');
            ++index;
        }
        output[count++] = value;
    }
    return count;
}

[[nodiscard]] std::uint64_t parse_u64(std::span<const std::byte> body) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < body.size(); ++index) {
        const unsigned char current = std::to_integer<unsigned char>(body[index]);
        if (current < '0' || current > '9') {
            if (value != 0) {
                break;
            }
            continue;
        }
        value = value * 10ULL + static_cast<std::uint64_t>(current - '0');
    }
    return value;
}

[[nodiscard]] bool handle_character_list(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    std::array<char, 2048> output{};
    std::size_t written = 0;
    if (!account_store::list_characters(output, written)) {
        return copy_response("No characters.", kHttpUnavailable, request, response);
    }
    return copy_response(std::string_view(output.data(), written), kHttpOk, request, response);
}

[[nodiscard]] bool handle_character_delete(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t soid = parse_u64(request.body);
    if (soid == 0) {
        return copy_response("Body must be one character soid.", kHttpBadRequest, request, response);
    }
    if (!account_store::delete_character(soid)) {
        return copy_response("Delete refused.", kHttpBadRequest, request, response);
    }
    return copy_response("deleted", kHttpOk, request, response);
}

[[nodiscard]] bool handle_character_create(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    std::array<unsigned, 3> values{};
    if (parse_numbers(request.body, values) != values.size() || values[0] > 2 || values[1] > 2
        || values[2] > 2) {
        return copy_response("Body must be 'class race gender', each 0..2.",
                             kHttpBadRequest,
                             request,
                             response);
    }
    if (!account_store::create_character(static_cast<std::uint8_t>(values[0]),
                                         static_cast<std::uint8_t>(values[1]),
                                         static_cast<std::uint8_t>(values[2]))) {
        return copy_response("Create refused.", kHttpBadRequest, request, response);
    }
    return copy_response("created", kHttpOk, request, response);
}

[[nodiscard]] bool handle_own_characters(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    const std::uint64_t platformId =
        now != 0 ? account_link_platform(request, now, binding) : 0;
    if (platformId == 0) {
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    }
    std::array<char, 1024> output{};
    std::size_t written = 0;
    if (!account_store::own_characters(platformId, output, written)) {
        return copy_response("Unable to read characters.", kHttpUnavailable, request, response);
    }
    return copy_response(std::string_view(output.data(), written), kHttpOk, request, response);
}

[[nodiscard]] bool handle_character_customize(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    constexpr std::size_t kProofSize = dawn::core::identity::device_identity::kProofSize;
    const std::uint64_t now = now_seconds();
    account_store::DeviceBinding binding{};
    if (now == 0 || request.body.size() <= kProofSize
        || !authenticate_device(dawn::core::identity::device_identity::Scope::discordLinkCode,
                                request.body.first(kProofSize),
                                now,
                                binding)) {
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    }
    const std::uint64_t soid = parse_u64(request.body.subspan(kProofSize));
    if (!account_store::mark_customize(binding.platformId, soid)) {
        return copy_response("Customize refused.", kHttpBadRequest, request, response);
    }
    return copy_response(soid != 0 ? "marked" : "cleared", kHttpOk, request, response);
}

[[nodiscard]] bool handle_weapon_acquire(const dawn::client::network::HttpRequest& request,
                                        dawn::client::network::HttpResponse& response) noexcept {
    constexpr auto proofSize = dawn::core::identity::device_identity::kProofSize;
    account_store::DeviceBinding binding{};
    if (request.body.size() <= proofSize || request.body.size() > proofSize + 512
        || !authenticate_device(dawn::core::identity::device_identity::Scope::weaponAcquire,
                                request.body.first(proofSize), now_seconds(), binding))
        return copy_response("Authentication failed.", kHttpUnauthorized, request, response);
    std::string_view input(reinterpret_cast<const char*>(request.body.data() + proofSize), request.body.size() - proofSize);
    std::array<std::uint64_t, 16> fields{};
    std::size_t count = 0;
    while (!input.empty()) {
        while (!input.empty() && input.front() == ' ') input.remove_prefix(1);
        if (input.empty()) break;
        if (count == fields.size()) return copy_response("Malformed acquire request.", kHttpBadRequest, request, response);
        const auto end = input.find(' ');
        const auto token = input.substr(0, end);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), fields[count]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
            return copy_response("Malformed acquire request.", kHttpBadRequest, request, response);
        ++count;
        input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
    }
    if (count < 4 || fields[3] > 12 || count != 4 + fields[3] || fields[2] > UINT32_MAX)
        return copy_response("Malformed acquire request.", kHttpBadRequest, request, response);
    account_store::WeaponAcquireRequest acquire{};
    acquire.requestId = fields[0]; acquire.characterSoid = fields[1];
    acquire.definitionHash = static_cast<std::uint32_t>(fields[2]); acquire.plugCount = static_cast<std::size_t>(fields[3]);
    for (std::size_t lane = 0; lane < acquire.plugCount; ++lane) {
        if (fields[4 + lane] > UINT32_MAX) return copy_response("Malformed plug hash.", kHttpBadRequest, request, response);
        acquire.plugs[lane] = static_cast<std::uint32_t>(fields[4 + lane]);
    }
    account_store::WeaponAcquireResult result{};
    std::string_view error{};
    if (!account_store::acquire_weapon(binding.platformId, acquire, result, error))
        return copy_response(error,
            error == "Select the current character before acquiring." || error == "Character inventory is full."
                || error == "Weapon bucket is full or the roll cannot be resolved."
                || error == "Request ID already used for another roll." ? 409 : kHttpBadRequest, request, response);
    std::array<char, 160> output{};
    const int size = std::snprintf(output.data(), output.size(), "acquired %llu %llu %u %llu",
        static_cast<unsigned long long>(result.characterSoid), static_cast<unsigned long long>(result.instanceSoid),
        result.definitionHash, static_cast<unsigned long long>(result.revision));
    return copy_response(std::string_view(output.data(), static_cast<std::size_t>(size)), kHttpOk, request, response);
}

void match_display_name(std::uint64_t account,std::array<char,64>& name) noexcept {
    struct CachedName {std::uint64_t account{},until{};std::array<char,64> name{};};
    static SRWLOCK nameLock=SRWLOCK_INIT;
    static std::array<CachedName,256> names{};
    const auto now=GetTickCount64();
    AcquireSRWLockShared(&nameLock);
    for(const auto& cached:names) if(cached.account==account && now<cached.until) {
        name=cached.name;ReleaseSRWLockShared(&nameLock);return;
    }
    ReleaseSRWLockShared(&nameLock);
    std::uint64_t platform{},revision{};
    thread_local dawn::state::AccountState profile{};
    if(!account_store::profile_by_account(account,platform,profile,revision,name)) return;
    name.back()=0;
    AcquireSRWLockExclusive(&nameLock);
    auto* slot=&names[0];
    for(auto& cached:names) {
        if(cached.account==account || cached.account==0) {slot=&cached;break;}
        if(cached.until<slot->until) slot=&cached;
    }
    *slot={account,now+60'000,name};
    ReleaseSRWLockExclusive(&nameLock);
}
} // namespace

bool consume(const dawn::client::network::HttpRequest& request,
             dawn::client::network::HttpResponse& response) noexcept {
    if (request.method == dawn::client::network::HttpMethod::post
        && request.url == "/v1/transport/certificate") {
        namespace identity = dawn::core::identity::device_identity;
        account_store::DeviceBinding binding{};
        std::size_t proofSize{};
        const auto now = now_seconds();
        const auto prefix = request.body.first((std::min)(request.body.size(), identity::kProofSize));
        if (!authenticate_social_identity(identity::Scope::transportCertificate, prefix, now, binding, proofSize)
            || proofSize >= request.body.size()) {
            response.statusCode = kHttpUnauthorized; response.size = 0; return true;
        }
        namespace publication = dawn::middleware::bap::native_certificate_publication;
        auto certificate = request.body.subspan(proofSize);
        const bool requestDurability = publication::requested(certificate);
        if (requestDurability) {
            if (request.response.size() < publication::kDurableAck.size()) {
                response.statusCode = kHttpBadRequest; response.size = 0; return true;
            }
            certificate = certificate.subspan(publication::kRequest.size());
        }
        (void)dawn::server::transport::native_certificate_cache::initialize_storage();
        bool durable{};
        const bool stored = dawn::server::transport::native_certificate_cache::publish(
            binding.platformId, certificate, now, &durable);
        response.statusCode = stored ? kHttpOk : kHttpBadRequest;
        response.size = 0;
        if (stored && requestDurability) {
            const auto& ack = durable ? publication::kDurableAck : publication::kVolatileAck;
            std::copy(ack.begin(), ack.end(), request.response.begin());
            response.size = ack.size();
        }
        dawn::core::log::write(dawn::core::log::Channel::server,
            stored ? dawn::core::log::Level::info : dawn::core::log::Level::warn,
            stored ? (durable ? "ev=steam_certificate stage=cache result=ok durable=1 native_verification=pending"
                             : "ev=steam_certificate stage=cache result=ok durable=0 native_verification=pending")
                   : "ev=steam_certificate stage=cache result=refused");
        return true;
    }
    if (request.method == dawn::client::network::HttpMethod::post
        && request.url == "/v1/bootstrap"
        && request.body.size() != dawn::middleware::content::bootstrap_configuration::kTokenSize) {
        namespace wire = dawn::middleware::content::bootstrap_configuration;
        namespace identity = dawn::core::identity::device_identity;
        wire::View metadata{};
        if (!wire::parse(request.body, metadata)) {
            response.statusCode = kHttpBadRequest; response.size = 0; return true;
        }
        if (metadata.proof.size() != identity::kProofSize) return false;
        identity::Claims claims{};
        account_store::DeviceBinding binding{};
        const auto now = now_seconds();
        if (!identity::verify(identity::Scope::contentBootstrap, metadata.proof, now, claims)
            || !authenticate_device(identity::Scope::contentBootstrap, metadata.proof, now, binding)) {
            response.statusCode = kHttpUnauthorized; response.size = 0; return true;
        }
        dawn::server::http::content_configuration::Snapshot content{};
        const std::size_t required = metadata.requestsNativeAcknowledgment ? wire::kAcknowledgmentSize : wire::kGuidSize;
        if (request.response.size() < required
            || !dawn::server::http::content_configuration::snapshot(claims.deviceId, binding.platformId, content)
            || !dawn::server::http::content_configuration::publish(claims.deviceId, binding.platformId, metadata.url)
            || !dawn::state::publish_bootstrap_token(metadata.token)) {
            response.statusCode = kHttpUnavailable; response.size = 0; return true;
        }
        if (!wire::acknowledge(metadata.requestsNativeAcknowledgment, content.guid.data(), request.response, response.size)) {
            response.statusCode = kHttpUnavailable; response.size = 0; return true;
        }
        response.statusCode = kHttpOk; response.contentType = "application/octet-stream";
        dawn::core::log::write(dawn::core::log::Channel::server, dawn::core::log::Level::info,
            "ev=http method=post route=bootstrap result=ok configuration=device");
        return true;
    }
    if(request.method==dawn::client::network::HttpMethod::post && request.url=="/v1/crucible/match") {
        namespace identity=dawn::core::identity::device_identity;
        namespace host=dawn::state::activity::crucible::host;
        account_store::DeviceBinding binding{};std::size_t proofSize{};
        if(!authenticate_social_identity(identity::Scope::crucibleMatch,request.body,now_seconds(),binding,proofSize)
            || request.body.size()!=proofSize) {
            response.statusCode=kHttpUnauthorized;response.size=0;return true;
        }
        host::View view{};
        (void)dawn::state::activity::crucible::runtime::for_account(binding.accountSoid,view);
        for(std::size_t i=0;i<view.count;++i) {
            match_display_name(view.rows[i].account,view.rows[i].name);
        }
        const bool encoded=dawn::middleware::crucible::match_view_wire::encode(view,request.response,response.size);
        response.statusCode=encoded?kHttpOk:kHttpUnavailable;
        response.contentType="application/octet-stream";return true;
    }
    if (platform_api::consume(request, response)) {
        return true;
    }
    if (request.method == dawn::client::network::HttpMethod::get
        && request.url == kDiscordStatusPath) return handle_discord_status(request, response);
    if (request.method == dawn::client::network::HttpMethod::get
        && request.url == kBotPendingPath) return handle_bot_pending(request, response);
    if (request.method == dawn::client::network::HttpMethod::get
        && request.url == kBotRolesPath) {
        std::string roleIds;
        roles::append_role_ids_json(roleIds);
        const bool handled = copy_response(roleIds, kHttpOk, request, response);
        response.contentType = "application/json";
        return handled;
    }
    if (request.method == dawn::client::network::HttpMethod::get
        && request.url == kCharactersPath) return handle_character_list(request, response);
    if (request.method != dawn::client::network::HttpMethod::post) return false;
    if (request.url == "/v1/account/weapon/acquire") return handle_weapon_acquire(request, response);
    if (request.url == kCharacterDeletePath) {
        return handle_character_delete(request, response);
    }
    if (request.url == kCharacterCreatePath) {
        return handle_character_create(request, response);
    }
    if (request.url == kOwnCharactersPath) {
        return handle_own_characters(request, response);
    }
    if (request.url == kCharacterCustomizePath) {
        return handle_character_customize(request, response);
    }
    if (request.url == kExpansionsPath) {
        return restoration::expansions::handle_submit(request, response);
    }
    if (request.url == kExpansionsResultPath || request.url == kJobsResultPath) {
        return restoration::expansions::handle_result(request, response);
    }
    if (request.url == kLauncherProfilePath) {
        return restoration::expansions::handle_profile(request, response);
    }
    if (request.url == kCodesIssuePath) {
        return restoration::expansions::handle_issue_code(request, response);
    }
    if (request.url == kCodesRedeemPath) {
        return restoration::expansions::handle_redeem_code(request, response);
    }
    if (request.url == kCodesForgetPath) {
        return restoration::expansions::handle_forget_access(request, response);
    }
    if (request.url == kLinkCodePath) {
        return handle_link_code(request, response);
    }
    if (request.url == kDiscordLinkStatePath) {
        return handle_link_state(request, response);
    }
    if (request.url == kRenamePath) {
        return handle_account_rename(request, response);
    }
    if (request.url == kDiscordUnlinkPath) {
        return handle_unlink_request(request, response);
    }
    if (route_matches(request.url, kSignOnPath)) {
        return handle_device_sign_on(request, response);
    }
    if (request.url == kFriendsPath) {
        return handle_social(request, response);
    }
    if (request.url == kRealtimePath) {
        return handle_device_realtime(request, response);
    }
    if (request.url == kProfilePath) {
        return handle_device_profile(request, response);
    }
    if (request.url == dawn::middleware::mission::observation_wire::kPath) {
        return handle_mission_observe(request, response);
    }
    if (request.url == kRelationshipsPath) {
        return handle_relationships(request, response);
    }
    if (request.url == kMessagesPath) {
        return handle_messages(request, response);
    }
    if (request.url == kAvatarsPath) {
        return handle_avatars(request, response);
    }
    if (request.url == kDiscordBotPath) {
        return handle_discord_bot(request, response);
    }
    return false;
}

} // namespace restoration::account_http
