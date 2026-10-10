#include "native_certificate_publish.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "../../../middleware/bap/native_certificate.h"
#include "../../../middleware/bap/native_certificate_publication.h"
#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../client/hooks/external_server/transport.h"

namespace dawn::steam::runtime::transport::native_certificate_publish {
namespace {
namespace wire = middleware::bap::native_certificate;
SRWLOCK g_lock{SRWLOCK_INIT};
std::array<std::byte, wire::kCapacity> g_certificate{};
std::size_t g_size{};
std::uint64_t g_platform{}, g_generation{}, g_acknowledged{}, g_durable{}, g_nextAttempt{};
std::atomic_flag g_servicing = ATOMIC_FLAG_INIT;
std::uint64_t now_seconds() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
}
void capture(std::span<const std::byte> callback, std::uint64_t platform) noexcept {
    std::array<std::byte, wire::kCapacity> certificate{};
    std::size_t size{};
    if (!wire::from_callback(callback, platform, now_seconds(), certificate, size)) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=steam_certificate stage=capture result=refused public_only=1");
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_certificate = certificate; g_size = size; g_platform = platform;
    ++g_generation; g_nextAttempt = 0;
    ReleaseSRWLockExclusive(&g_lock);
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=steam_certificate stage=capture result=ok public_only=1");
}
void service(std::uint64_t nowMilliseconds) noexcept {
    if (!core::settings::get().client.externalServer.enabled
        || g_servicing.test_and_set(std::memory_order_acquire)) return;
    std::array<std::byte, wire::kCapacity> certificate{};
    std::size_t size{};
    std::uint64_t generation{}, platform{};
    AcquireSRWLockExclusive(&g_lock);
    if (g_size != 0 && nowMilliseconds >= g_nextAttempt) {
        certificate = g_certificate; size = g_size;
        generation = g_generation; platform = g_platform;
        g_nextAttempt = nowMilliseconds + 5000;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (size == 0) { g_servicing.clear(std::memory_order_release); return; }
    namespace identity = core::identity::device_identity;
    std::array<std::byte, identity::kProofSize + 4 + wire::kCapacity> body{};
    std::size_t proofSize{};
    const auto now = now_seconds();
    bool authenticated = false;
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    proofSize = identity::kProofSize;
    authenticated = identity::platform_id() == platform && identity::issue(
        identity::Scope::transportCertificate, now,
        std::span<std::byte, identity::kProofSize>(body.data(), proofSize));
#else
    proofSize = core::identity::local_ticket::kTicketSize;
    authenticated = core::identity::local_ticket::issue(platform, now,
        std::span<std::byte, core::identity::local_ticket::kTicketSize>(body.data(), proofSize));
#endif
    bool acknowledged = false, durable = false;
    wire::Metadata metadata{};
    if (authenticated && wire::signed_metadata(std::span(certificate).first(size), platform, now, metadata)) {
        namespace publication = middleware::bap::native_certificate_publication;
        std::copy(publication::kRequest.begin(), publication::kRequest.end(), body.begin() + proofSize);
        std::copy_n(certificate.begin(), size, body.begin() + proofSize + publication::kRequest.size());
        std::array<std::byte, 4> responseBody{};
        client::network::HttpResponse response{};
        client::network::HttpRequest request{
            client::network::HttpMethod::post, "/v1/transport/certificate", "application/octet-stream", {},
            std::span(body).first(proofSize + publication::kRequest.size() + size), responseBody};
        const bool completed = client::hooks::external_server::consume(request, response);
        acknowledged = completed && response.statusCode == 200 && response.size <= responseBody.size()
            && publication::acknowledgement(std::span(responseBody).first(response.size), durable);
        // old servers reject the optional prefix as an invalid cert; retry once with the legacy
        // envelope (its empty ACK proves no durability)
        if (completed && response.statusCode == 400) {
            std::copy_n(certificate.begin(), size, body.begin() + proofSize);
            request.body = std::span(body).first(proofSize + size);
            response = {};
            acknowledged = client::hooks::external_server::consume(request, response)
                && response.statusCode == 200 && response.size == 0;
            durable = false;
        }
    }
    SecureZeroMemory(body.data(), proofSize);
    AcquireSRWLockExclusive(&g_lock);
    if (generation == g_generation && acknowledged) {
        g_acknowledged = generation;
        g_durable = durable ? generation : 0;
        g_nextAttempt = nowMilliseconds + 30000;
    }
    ReleaseSRWLockExclusive(&g_lock);
    core::log::write(core::log::Channel::client,
        acknowledged ? core::log::Level::info : core::log::Level::warn,
        acknowledged ? (durable ? "ev=steam_certificate stage=publish result=acknowledged durable=1 native_verification=pending"
                               : "ev=steam_certificate stage=publish result=acknowledged durable=0 native_verification=pending")
                     : "ev=steam_certificate stage=publish result=retry fallback=preserved");
    g_servicing.clear(std::memory_order_release);
}
bool published() noexcept {
    const auto now = now_seconds();
    wire::Metadata metadata{};
    AcquireSRWLockShared(&g_lock);
    const bool result = g_size != 0 && g_generation != 0 && g_generation == g_acknowledged
        && wire::signed_metadata(std::span(g_certificate).first(g_size), g_platform, now, metadata);
    ReleaseSRWLockShared(&g_lock);
    return result;
}
bool durably_published() noexcept {
    const auto now = now_seconds();
    wire::Metadata metadata{};
    AcquireSRWLockShared(&g_lock);
    const bool result = g_size != 0 && g_generation != 0 && g_generation == g_durable
        && wire::signed_metadata(std::span(g_certificate).first(g_size), g_platform, now, metadata);
    ReleaseSRWLockShared(&g_lock);
    return result;
}
bool durable_certificate_matches(std::span<const std::byte> nativePublicCertificate) noexcept {
    const auto now = now_seconds();
    wire::Metadata published{}, native{};
    AcquireSRWLockShared(&g_lock);
    const bool result = g_size != 0 && g_generation != 0 && g_generation == g_durable
        && wire::signed_metadata(std::span(g_certificate).first(g_size), g_platform, now, published)
        && wire::certificate_metadata(nativePublicCertificate, g_platform, now, native)
        && published.key == native.key && published.expires - now > 120 && native.expires - now > 120;
    ReleaseSRWLockShared(&g_lock);
    return result;
}
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_certificate = {}; g_size = 0; g_platform = 0; g_acknowledged = 0; g_durable = 0; g_nextAttempt = 0;
    ++g_generation; // a response from an older bridge must not ack a new one
    ReleaseSRWLockExclusive(&g_lock);
}
}
