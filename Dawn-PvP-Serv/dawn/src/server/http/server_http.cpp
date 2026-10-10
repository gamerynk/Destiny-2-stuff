#include "server_http.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../core/identity/local_ticket.h"
#include "../../middleware/content/manifest/encoder.h"
#include "../../middleware/signon/response.h"
#include "../../middleware/social/friend_snapshot_codec.h"
#include "../../state/content_manifest/content_manifest_state_runtime.h"
#include "../../state/entitlements/entitlement_runtime.h"
#include "../../state/runtime/runtime.h"
#include "../../state/session/session_runtime.h"
#include "../../state/social/social_state.h"
#include "../../steam/runtime/runtime.h"
#include "owner_dashboard.h"
#include "content_configuration.h"
#include "../transport/native_certificate_cache.h"
#include "../../middleware/content/bootstrap_configuration.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::http {
namespace {

constexpr std::string_view kSignOnPath = "/SignOn";
constexpr std::string_view kBootstrapPath = "/v1/bootstrap";
constexpr std::string_view kConfigPath = "/config/";
constexpr std::string_view kHealthPath = "/healthz";
constexpr std::string_view kReadyPath = "/readyz";
constexpr std::string_view kOwnerPath = "/owner";
constexpr std::string_view kFriendsPath = "/v1/social/friends";
constexpr std::string_view kRealtimePath = "/v1/social/realtime";
constexpr std::string_view kProfilePath = "/v1/social/profile";
constexpr std::string_view kPlatformMountPoint = "/Platform";
constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpUnavailable = 503;

std::atomic<ExtensionConsumer> g_extensionConsumer{nullptr};

struct ManifestContext final {
    std::span<std::byte> output;
    std::size_t size{};
};

[[nodiscard]] bool encode_manifest(void* value,
                                   const state::content_manifest::View& view) noexcept {
    auto& context = *static_cast<ManifestContext*>(value);
    return middleware::content::manifest::encode(state::entitlements::get(),
                                                 view.rows,
                                                 view.guid,
                                                 context.output,
                                                 context.size);
}

[[nodiscard]] bool copy_manifest_guid(void* value,
                                      const state::content_manifest::View& view) noexcept {
    auto& context = *static_cast<ManifestContext*>(value);
    if (view.guid.size() > context.output.size()) {
        return false;
    }
    std::copy(view.guid.begin(), view.guid.end(),
              reinterpret_cast<char*>(context.output.data()));
    context.size = view.guid.size();
    return true;
}

[[nodiscard]] bool write_text(std::string_view body,
                              client::network::HttpResponse& response,
                              std::span<std::byte> output) noexcept {
    if (body.size() > output.size()) {
        return false;
    }
    std::copy(body.begin(), body.end(), reinterpret_cast<char*>(output.data()));
    response.size = body.size();
    response.contentType = "application/json";
    return true;
}

[[nodiscard]] bool route_matches(std::string_view url, std::string_view path) noexcept {
    return url == path
           || (url.size() > path.size() && url.starts_with(path)
               && url[path.size()] == '?');
}

[[nodiscard]] bool public_route(const client::network::HttpRequest& request) noexcept {
    // the Platform surface is public by design (third-party tools call it) and has its own API-key gate
    // and per-route auth, so the whole mount point is allowed and it refuses paths it doesn't serve
    if (request.url.starts_with(kPlatformMountPoint)
        && (request.url.size() == kPlatformMountPoint.size()
            || request.url[kPlatformMountPoint.size()] == '/')) {
        return true;
    }
    if (request.method == client::network::HttpMethod::get) {
        return route_matches(request.url, kHealthPath)
               || route_matches(request.url, kReadyPath)
               || route_matches(request.url, kFriendsPath)
               || request.url == kConfigPath
               || (request.url.size() > kConfigPath.size()
                   && request.url.starts_with(kConfigPath));
    }
    return route_matches(request.url, kBootstrapPath)
           || (request.method == client::network::HttpMethod::post
               && request.url == "/v1/transport/certificate")
           || route_matches(request.url, kSignOnPath)
           || route_matches(request.url, "/v1/account/discord-link-code")
           || route_matches(request.url, "/v1/account/discord-link-state")
           || (request.method == client::network::HttpMethod::post
               && request.url == "/v1/account/name")
           || (request.method == client::network::HttpMethod::post
               && request.url == "/v1/account/weapon/acquire")
           || route_matches(request.url, "/v1/account/discord-unlink")
           || route_matches(request.url, "/v1/account/characters/mine")
           || route_matches(request.url, "/v1/account/character/customize")
           || route_matches(request.url, "/v1/account/expansions/steam")
           || route_matches(request.url, "/v1/account/expansions/steam/result")
           || route_matches(request.url, "/v1/account/jobs/result")
           || route_matches(request.url, "/v1/account/launcher/profile")
           || route_matches(request.url, "/v1/account/codes/issue")
           || route_matches(request.url, "/v1/account/codes/redeem")
           || route_matches(request.url, "/v1/account/codes/forget")
           || route_matches(request.url, kFriendsPath)
           || route_matches(request.url, kRealtimePath)
           || route_matches(request.url, kProfilePath)
           || route_matches(request.url, "/v1/social/relationships")
           || route_matches(request.url, "/v1/social/messages")
           || route_matches(request.url, "/v1/social/avatars")
           || route_matches(request.url, "/v1/mission/observe")
           || route_matches(request.url, "/v1/pve/observe")
           || (request.method == client::network::HttpMethod::post
               && request.url == "/v1/crucible/match");
}

} // namespace

bool register_extension_consumer(ExtensionConsumer consumer) noexcept {
    ExtensionConsumer expected = nullptr;
    return consumer != nullptr
           && g_extensionConsumer.compare_exchange_strong(
               expected, consumer, std::memory_order_acq_rel, std::memory_order_acquire);
}

void unregister_extension_consumer(ExtensionConsumer consumer) noexcept {
    ExtensionConsumer expected = consumer;
    if (g_extensionConsumer.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire)) {
        content_configuration::clear();
        transport::native_certificate_cache::clear();
    }
}

bool consume(const client::network::HttpRequest& request,
             client::network::HttpResponse& response) noexcept {
    if (const ExtensionConsumer extension =
            g_extensionConsumer.load(std::memory_order_acquire);
        extension != nullptr && extension(request, response)) {
        return true;
    }
    if (request.method == client::network::HttpMethod::get
        && request.url == kFriendsPath) {
        const state::social::Snapshot social = state::social::snapshot();
        response.statusCode = kHttpOk;
        response.contentType = "application/octet-stream";
        if (!middleware::social::friend_snapshot::encode(
                social, request.response, response.size)) {
            response.statusCode = kHttpUnavailable;
            response.size = 0;
        }
        return true;
    }
    if (request.method == client::network::HttpMethod::get
        && (request.url == kOwnerPath
            || (request.url.size() > kOwnerPath.size()
                && request.url.starts_with(kOwnerPath)
                && request.url[kOwnerPath.size()] == '?'))) {
        response.statusCode = kHttpOk;
        response.contentType = "text/html; charset=utf-8";
        return owner_dashboard::render(request.response, response.size);
    }
    if (request.method == client::network::HttpMethod::get
        && request.url.find(kHealthPath) != std::string_view::npos) {
        response.statusCode = kHttpOk;
        return write_text("{\"status\":\"ok\"}\n", response, request.response);
    }
    if (request.method == client::network::HttpMethod::get
        && request.url.find(kReadyPath) != std::string_view::npos) {
        const bool ready = state::bootstrap_token_present();
        response.statusCode = ready ? kHttpOk : kHttpUnavailable;
        return write_text(ready ? "{\"ready\":true}\n" : "{\"ready\":false}\n",
                          response,
                          request.response);
    }
    if (request.method == client::network::HttpMethod::post
        && request.url.find(kBootstrapPath) != std::string_view::npos) {
        auto token = request.body;
        bool nativeAcknowledgment = false;
        if (token.size() != middleware::content::bootstrap_configuration::kTokenSize) {
            middleware::content::bootstrap_configuration::View metadata{};
            core::identity::local_ticket::Claims claims{};
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (!middleware::content::bootstrap_configuration::parse(request.body, metadata)
                || metadata.proof.size() != core::identity::local_ticket::kTicketSize || now <= 0
                || !core::identity::local_ticket::authenticate(metadata.proof, static_cast<std::uint64_t>(now), claims)
                || claims.provider != core::identity::local_ticket::kProviderSteam
                || claims.platformId != steam::local_steam_id()) {
                response.statusCode = kHttpUnauthorized; response.size = 0; return true;
            }
            nativeAcknowledgment = metadata.requestsNativeAcknowledgment;
            const std::size_t required = nativeAcknowledgment
                ? middleware::content::bootstrap_configuration::kAcknowledgmentSize : 36U;
            if (request.response.size() < required
                || !content_configuration::publish(0, claims.platformId, metadata.url)) {
                response.statusCode = kHttpUnavailable; response.size = 0; return true;
            }
            token = metadata.token;
        }
        if (!state::publish_bootstrap_token(token)) {
            response.statusCode = kHttpUnavailable;
            response.size = 0;
            return true;
        }
        ManifestContext context{nativeAcknowledgment ? request.response.subspan(4) : request.response};
        if (!state::content_manifest::visit_snapshot(&copy_manifest_guid, &context)) {
            response.statusCode = kHttpUnavailable;
            response.size = 0;
            return true;
        }
        response.statusCode = kHttpOk;
        response.size = context.size;
        if (nativeAcknowledgment) {
            request.response[0] = std::byte{'D'}; request.response[1] = std::byte{'C'};
            request.response[2] = std::byte{'A'}; request.response[3] = std::byte{1};
            response.size += 4;
        }
        response.contentType = "text/plain";
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=http method=post route=bootstrap result=ok");
        return true;
    }
    if (request.method == client::network::HttpMethod::get
        && request.url.find(kConfigPath) != std::string_view::npos) {
        ManifestContext context{request.response};
        if (!state::content_manifest::visit_snapshot(&encode_manifest, &context)) {
            response.statusCode = kHttpUnavailable;
            response.size = 0;
            return true;
        }
        response.statusCode = kHttpOk;
        response.size = context.size;
        response.contentType = "application/octet-stream";
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=http method=get route=config result=ok");
        return true;
    }
    if (request.method != client::network::HttpMethod::post
        || request.url.find(kSignOnPath) == std::string_view::npos) {
        return false;
    }
    if (!state::bootstrap_token_present()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return true;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    core::identity::local_ticket::Claims identity{};
    if (now <= 0
        || !core::identity::local_ticket::authenticate(
            request.body, static_cast<std::uint64_t>(now), identity)
        || identity.provider != core::identity::local_ticket::kProviderSteam
        || identity.platformId != steam::local_steam_id()) {
        response.statusCode = kHttpUnauthorized;
        response.size = 0;
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=http method=post route=signon result=fail reason=identity_ticket");
        return true;
    }
    const state::AccountState account = state::account_snapshot();
    const state::SignOnState bootstrapState = state::sign_on();
    state::session::Credentials credentials{};
    if (account.primarySoid == 0
        || !state::session::issue(account.primarySoid,
                                  identity.platformId,
                                  bootstrapState,
                                  static_cast<std::uint64_t>(now),
                                  credentials)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=http method=post route=signon result=fail reason=session_issue");
        return true;
    }
    const auto serverTime = static_cast<std::uint64_t>(now);
    const std::uint64_t expiry = credentials.expiresAt;
    content_configuration::Snapshot content{};
    if (!content_configuration::snapshot(0, identity.platformId, content)
        || !middleware::signon::encode_success(credentials.signOn,
                                            state::entitlements::get(),
                                            expiry,
                                            serverTime,
                                            request.response,
                                            response.size,
                                            {content.url.data(), content.guid.data()})) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=http method=post route=signon stage=encode result=fail");
        return false;
    }
#if defined(RESTORATION_DEDICATED_SERVER)
    if (!::restoration::realtime_store::begin_game_session(identity.platformId,
                                                           account.primarySoid,
                                                           credentials.generation,
                                                           serverTime)) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        core::log::write(
            core::log::Channel::server,
            core::log::Level::warn,
            "ev=http method=post route=signon result=fail reason=realtime_generation_reset");
        return true;
    }
#endif
    response.statusCode = kHttpOk;
    core::log::write(core::log::Channel::server,
                     core::log::Level::info,
                     "ev=http method=post route=signon result=ok");
    return true;
}

bool consume_public(const client::network::HttpRequest& request,
                    client::network::HttpResponse& response) noexcept {
    return public_route(request) && consume(request, response);
}

} // namespace dawn::server::http
