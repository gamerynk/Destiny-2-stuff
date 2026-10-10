#include "bootstrap_token_publish.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <chrono>
#include <atomic>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../middleware/content/bootstrap_configuration.h"
#include "../../../steam/runtime/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../../hooks/config_getter/config_getter_answers.h"
#include "../../hooks/external_server/transport.h"
#include "../../targets/game.h"

namespace dawn::client::content::bootstrap {
namespace {

constexpr std::size_t kServerPublishAttempts = 8;
constexpr DWORD kServerPublishRetryDelayMilliseconds = 1500;
std::atomic_bool g_nativeConfigurationReady{false};

[[nodiscard]] bool publish_to_server(std::span<const std::byte> token,
                                     std::array<char, 37>& manifestGuid) noexcept {
    namespace wire = middleware::content::bootstrap_configuration;
    std::array<std::byte, wire::kAcknowledgmentSize> responseBody{};
    namespace identity = core::identity::device_identity;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now <= 0) return false;
    std::array<std::byte, identity::kProofSize> proof{};
    std::size_t proofSize{};
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    proofSize = identity::kProofSize;
    const bool authenticated = identity::issue(identity::Scope::contentBootstrap,
        static_cast<std::uint64_t>(now), proof);
#else
    proofSize = core::identity::local_ticket::kTicketSize;
    const bool authenticated = core::identity::local_ticket::initialize(GetModuleHandleW(nullptr))
        && core::identity::local_ticket::issue(steam::local_steam_id(),
            static_cast<std::uint64_t>(now),
            std::span<std::byte, core::identity::local_ticket::kTicketSize>(proof.data(), proofSize));
#endif
    std::array<std::byte, wire::kCapacity> metadata{};
    std::size_t metadataSize{};
    if (!authenticated || !wire::encode(token, std::span(proof).first(proofSize),
        core::settings::get().client.externalServer.configUrl.data(), metadata, metadataSize)) return false;
    network::HttpRequest request{
        network::HttpMethod::post,
        "/v1/bootstrap",
        "application/octet-stream",
        {},
        std::span(metadata).first(metadataSize),
        responseBody,
    };
    for (std::size_t attempt = 0; attempt < kServerPublishAttempts; ++attempt) {
        responseBody.fill(std::byte{});
        network::HttpResponse response{};
        bool consumed = hooks::external_server::consume(request, response);
        // older servers reject the metadata envelope but still accept the original 16-byte token
        if (consumed && (response.statusCode == 400 || response.statusCode == 404 || response.statusCode == 415)
            && request.body.size() != wire::kTokenSize) {
            request.body = token;
            responseBody.fill(std::byte{}); response = {};
            consumed = hooks::external_server::consume(request, response);
        }
        if (consumed && response.statusCode == 200 && response.size <= responseBody.size()) {
            std::string_view guid{}; bool native{};
            if (wire::parse_acknowledgment(std::span(responseBody).first(response.size), guid, native)) {
                std::copy(guid.begin(), guid.end(), manifestGuid.begin()); manifestGuid.back() = '\0';
                // a legacy token response cannot authorize native config, even if it returns an ACK
                g_nativeConfigurationReady.store(native && request.body.size() != wire::kTokenSize, std::memory_order_release);
                return true;
            }
        }

        if (attempt + 1 < kServerPublishAttempts) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::warn,
                             "ev=bootstrap stage=server result=retry");
            Sleep(kServerPublishRetryDelayMilliseconds);
        }
    }
    return false;
}

} // namespace

bool publish_token() noexcept {
    g_nativeConfigurationReady.store(false, std::memory_order_release);
    const std::byte* const source = targets::game::network::get().contentIdToken;
    if (source == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=bootstrap stage=token result=fail reason=target");
        return false;
    }
    std::array<std::byte, state::kBootstrapTokenSize> token{};
    __try {
        for (std::size_t index = 0; index < token.size(); ++index) {
            token[index] = source[index];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=bootstrap stage=token result=fail reason=read");
        return false;
    }
    const bool external = core::settings::get().client.externalServer.enabled;
    std::array<char, 37> manifestGuid{};
    // package readers need the token even with an external server, so publish before starting workers
    const bool published = state::publish_bootstrap_token(token)
        && (!external || publish_to_server(token, manifestGuid));
    const bool configured =
        !external || hooks::config_getter::publish_server_guid(manifestGuid.data());
    if (!published || !configured) {
        g_nativeConfigurationReady.store(false, std::memory_order_release);
        SecureZeroMemory(token.data(), token.size());
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         external
                             ? "ev=bootstrap stage=server result=fail reason=unavailable"
                             : "ev=bootstrap stage=token result=fail reason=publish");
        return false;
    }
    SecureZeroMemory(token.data(), token.size());
    core::log::write(
        core::log::Channel::client,
        core::log::Level::info,
        external ? "ev=bootstrap stage=server result=ok" : "ev=bootstrap stage=token result=ok");
    return true;
}

bool native_configuration_ready() noexcept {
    return g_nativeConfigurationReady.load(std::memory_order_acquire);
}

} // namespace dawn::client::content::bootstrap
