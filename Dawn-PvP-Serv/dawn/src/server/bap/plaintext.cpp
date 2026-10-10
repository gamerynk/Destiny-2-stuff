#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdio>

#include "../../core/logging/log.h"
#include "../../middleware/secure_channel/runtime.h"
#include "../../state/matchmaking/matchmaking_state.h"
#include "../../state/runtime/runtime.h"
#include "encrypted/internal.h"
#include "internal.h"
#include "server_hello_shape.h"

namespace dawn::server::bap::plaintext {
namespace {

constexpr std::size_t kTokenOffset = 2;
constexpr std::size_t kProtocolTagOffset = kTokenOffset + state::kSessionTokenSize;
constexpr std::size_t kProtocolVersionOffset = kProtocolTagOffset + 1;
static_assert(state::kSessionTokenSize == 32);
constexpr std::byte kReceiveDirectionMask{0x01};

// a refused server hello leaves the link stuck in _authenticating until activity setup times out, and that names no service
void report_refusal(const Session& session, std::uint16_t service, const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bap svc=%u stage=plaintext result=fail conn=%u "
                                      "reason=%s",
                                      static_cast<unsigned>(service),
                                      session.id,
                                      reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

// log only the framing bytes; the token they wrap is a secret
void report_hello_shape(const Session& session, std::span<const std::byte> body) noexcept {
    const auto at = [body](std::size_t index) noexcept {
        return index < body.size() ? std::to_integer<unsigned>(body[index]) : 0U;
    };
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bap svc=25 stage=plaintext result=fail reason=shape "
                                      "conn=%u len=%zu b0=%02X b1=%02X b34=%02X b35=%02X",
                                      session.id,
                                      body.size(),
                                      at(0),
                                      at(1),
                                      at(kProtocolTagOffset),
                                      at(kProtocolVersionOffset));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool valid_hello_shape(std::span<const std::byte> body) noexcept {
    return hello_shape::valid(body);
}

[[nodiscard]] std::span<const std::byte, state::kSessionTokenSize>
hello_token(std::span<const std::byte> body) noexcept {
    return std::span<const std::byte, state::kSessionTokenSize>(
        body.data() + kTokenOffset, state::kSessionTokenSize);
}

void arm_encryption(Session& session, const state::session::Credentials& credentials) noexcept {
    session.credentials = credentials;
    session.sendNonce = credentials.bap.nonce;
    session.receiveNonce = credentials.bap.nonce;
    session.receiveNonce.back() ^= kReceiveDirectionMask;
    session.authenticated = true;
}

[[nodiscard]] bool consume_service(Session& session,
                                   Scratch& scratch,
                                   const middleware::bap::RequestFrame& request,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept {
    written = 0;
    encrypted::ServiceRoute route;
    (void)encrypted::routing::resolve(request.messageId, route);
    if (route.responseMode != encrypted::ResponseMode::reply) {
        core::log::write(core::log::Channel::server, core::log::Level::info, route.successEvent);
        return true;
    }
    encrypted::ServiceOutcome outcome{};
    std::size_t bodySize = 0;
    // a refusing codec still answers with an empty body; the client matches only its pending ring head, so one unanswered request jams it
    if (!encrypted::body::process(route,
                                  session.queuez,
                                  session.activitySessionId,
                                  session.matchmakingContext,
                                  session.credentials.accountSoid,
                                  session.credentials.platformId,
                                  request.body,
                                  scratch.responseBody,
                                  bodySize,
                                  outcome)) {
        bodySize = 0;
    }
    const bool encoded =
        middleware::bap::encode_response(route.response,
                                         request.taskId,
                                         request.frameType,
                                         std::span(scratch.responseBody).first(bodySize),
                                         response,
                                         written);
    SecureZeroMemory(scratch.responseBody.data(), bodySize);
    SecureZeroMemory(&outcome, sizeof outcome);
    if (!encoded) {
        report_refusal(session, request.messageId, "encode");
        return false;
    }
    core::log::write(core::log::Channel::server, core::log::Level::info, route.successEvent);
    return true;
}

} // namespace

bool consume(Session& session,
             Scratch& scratch,
             const middleware::bap::OuterFrame& outer,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    written = 0;
    middleware::bap::RequestFrame frame;
    if (!middleware::bap::parse_request_payload(outer.payload, outer.frameType, frame)) {
        report_refusal(session, 0, "parse");
        return false;
    }
    // the channel-start body is a nonce the client checks only by length, so echoing it is always right
    if (frame.messageId == static_cast<std::uint16_t>(middleware::bap::RequestService::start)) {
        const bool encoded =
            middleware::bap::encode_response(middleware::bap::ResponseService::start,
                                             frame.taskId,
                                             frame.frameType,
                                             frame.body,
                                             response,
                                             written);
        if (encoded) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             "ev=bap svc=30 rsp=31 result=ok");
        }
        return encoded;
    }
    if (frame.messageId
        != static_cast<std::uint16_t>(middleware::bap::RequestService::serverHello)) {
        if (!session.authenticated) {
            report_refusal(session, frame.messageId, "unauthenticated");
            return false;
        }
        return consume_service(session, scratch, frame, response, written);
    }

    if (session.authenticated) {
        report_refusal(session, frame.messageId, "reauthenticated");
        return false;
    }
    if (!valid_hello_shape(frame.body)) {
        report_hello_shape(session, frame.body);
        return false;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    state::session::Credentials credentials{};
    if (now <= 0
        || !state::session::authenticate(
            hello_token(frame.body), static_cast<std::uint64_t>(now), credentials)
        || credentials.accountSoid == 0) {
        report_refusal(session, frame.messageId, "token_or_account");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    state::AccountRuntimeContext accountRuntime{};
    const state::AccountState owner = state::account_snapshot();
    if (credentials.accountSoid != owner.primarySoid
        && !state::resolve_account_runtime(credentials.accountSoid,
                                           credentials.platformId,
                                           accountRuntime)) {
        report_refusal(session, frame.messageId, "account_context");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    const state::ScopedAccountRuntime accountScope(accountRuntime);
    const state::AccountState account = state::account_snapshot();
    std::uint64_t boundAccountSoid{};
    const auto authenticatedRuntime = state::capture_account_runtime(boundAccountSoid);
    if (credentials.accountSoid != account.primarySoid
        || boundAccountSoid != credentials.accountSoid
        || !authenticatedRuntime.account || !authenticatedRuntime.activity) {
        report_refusal(session, frame.messageId, "account_binding");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    // a repeat hello replaces its context; release the old one first or the small pool drains
    if (session.matchmakingContext.generation != state::matchmaking::kInvalidGeneration) {
        (void)state::matchmaking::release_context(session.matchmakingContext);
        session.matchmakingContext = {};
    }
    state::matchmaking::ContextHandle matchmakingContext{};
    if (!state::matchmaking::acquire_context(matchmakingContext)) {
        report_refusal(session, frame.messageId, "context_unavailable");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    std::array<std::byte, middleware::secure_channel::kServerHelloEnvelopeSize> envelope{};
    std::size_t envelopeSize = 0;
    const bool encoded =
        middleware::secure_channel::encode_server_hello(
            credentials.signOn, credentials.bap, envelope, envelopeSize)
        && middleware::bap::encode_response(middleware::bap::ResponseService::serverHello,
                                            frame.taskId,
                                            frame.frameType,
                                            std::span(envelope).first(envelopeSize),
                                            response,
                                            written);
    SecureZeroMemory(envelope.data(), envelope.size());
    if (!encoded) {
        (void)state::matchmaking::release_context(matchmakingContext);
        report_refusal(session, frame.messageId, "encode");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    // clear a cached previous-generation pick once, after authenticating its owner; later hellos must keep the real pick
    if (!state::session::initialize_game_generation(
            credentials, static_cast<std::uint64_t>(now),
            [](void*, std::uint64_t accountSoid) noexcept {
                return state::begin_character_selection(accountSoid);
            }, nullptr)) {
        (void)state::matchmaking::release_context(matchmakingContext);
        written = 0;
        report_refusal(session, frame.messageId, "character_generation");
        SecureZeroMemory(&credentials, sizeof credentials);
        return false;
    }
    session.matchmakingContext = matchmakingContext;
    session.accountRuntime = authenticatedRuntime;
    arm_encryption(session, credentials);
    SecureZeroMemory(&credentials, sizeof credentials);
    core::log::write(
        core::log::Channel::server, core::log::Level::info, "ev=bap svc=25 rsp=26 result=ok");
    return true;
}

} // namespace dawn::server::bap::plaintext
