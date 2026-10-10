#include "session_runtime.h"
#include "native_bap_endpoint.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <limits>

#include "../../core/settings/settings.h"

namespace dawn::state::session {
namespace {

constexpr std::size_t kCapacity = 8;
constexpr std::uint32_t kLifetimeSeconds = 60U * 60U;

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Credentials, kCapacity> g_sessions{};
std::array<bool, kCapacity> g_generationInitialized{};
std::uint64_t g_nextGeneration{1};

template <std::size_t Size>
[[nodiscard]] bool randomize(std::array<std::byte, Size>& output) noexcept {
    return BCryptGenRandom(nullptr,
                           reinterpret_cast<PUCHAR>(output.data()),
                           static_cast<ULONG>(output.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG)
           >= 0;
}

[[nodiscard]] bool token_matches(std::span<const std::byte, kSessionTokenSize> left,
                                 const std::array<std::byte, kSessionTokenSize>& right) noexcept {
    unsigned difference = 0;
    for (std::size_t index = 0; index < right.size(); ++index) {
        difference |= std::to_integer<unsigned>(left[index] ^ right[index]);
    }
    return difference == 0;
}

} // namespace

bool issue(std::uint64_t accountSoid,
           std::uint64_t platformId,
           const SignOnState& bootstrapState,
           std::uint64_t nowSeconds,
           Credentials& credentials) noexcept {
    credentials = {};
    if (accountSoid == 0 || platformId == 0 || !bootstrapState.bootstrapTokenPresent
        || nowSeconds > (std::numeric_limits<std::uint64_t>::max)() - kLifetimeSeconds) {
        return false;
    }
    Credentials issued{};
    if (!randomize(issued.signOn.encryptionKey)
        || !randomize(issued.signOn.authenticationKey)
        || !randomize(issued.signOn.sessionToken) || !randomize(issued.bap.nonce)
        || !randomize(issued.bap.sessionKey) || !randomize(issued.bap.envelopeIv)) {
        SecureZeroMemory(&issued, sizeof issued);
        return false;
    }
    publish_native_bap_endpoint(issued.signOn, core::settings::get());
    issued.signOn.tokenLifetimeSeconds = kLifetimeSeconds;
    issued.signOn.bootstrapToken = bootstrapState.bootstrapToken;
    issued.signOn.bootstrapTokenPresent = true;
    issued.accountSoid = accountSoid;
    issued.platformId = platformId;
    issued.issuedAt = nowSeconds;
    issued.expiresAt = nowSeconds + kLifetimeSeconds;

    AcquireSRWLockExclusive(&g_lock);
    std::size_t slot = g_sessions.size();
    std::size_t reusable = g_sessions.size();
    std::size_t oldest = 0;
    for (std::size_t index = 0; index < g_sessions.size(); ++index) {
        const Credentials& existing = g_sessions[index];
        // one platform identity owns one live SignOn generation; reissuing revokes the old
        // credentials instead of leaving two BAP sessions authoritative
        if (existing.platformId == platformId) {
            slot = index;
            break;
        }
        if (existing.accountSoid == 0 || existing.platformId == 0
            || existing.expiresAt <= nowSeconds) {
            if (reusable == g_sessions.size()) {
                reusable = index;
            }
            continue;
        }
        if (existing.expiresAt < g_sessions[oldest].expiresAt) {
            oldest = index;
        }
    }
    if (slot == g_sessions.size()) {
        slot = reusable != g_sessions.size() ? reusable : oldest;
    }
    issued.generation = g_nextGeneration++;
    if (issued.generation == 0) {
        issued.generation = g_nextGeneration++;
    }
    SecureZeroMemory(&g_sessions[slot], sizeof g_sessions[slot]);
    g_sessions[slot] = issued;
    g_generationInitialized[slot] = false;
    credentials = issued;
    ReleaseSRWLockExclusive(&g_lock);
    SecureZeroMemory(&issued, sizeof issued);
    return true;
}

bool authenticate(std::span<const std::byte, kSessionTokenSize> token,
                  std::uint64_t nowSeconds,
                  Credentials& credentials) noexcept {
    credentials = {};
    AcquireSRWLockShared(&g_lock);
    bool found = false;
    for (const Credentials& candidate : g_sessions) {
        if (candidate.accountSoid != 0 && candidate.platformId != 0
            && candidate.issuedAt <= nowSeconds
            && candidate.expiresAt > nowSeconds
            && token_matches(token, candidate.signOn.sessionToken)) {
            credentials = candidate;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool active(const Credentials& credentials, std::uint64_t nowSeconds) noexcept {
    if (credentials.accountSoid == 0 || credentials.platformId == 0
        || credentials.generation == 0 || credentials.issuedAt > nowSeconds
        || credentials.expiresAt <= nowSeconds) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    bool found = false;
    for (const Credentials& candidate : g_sessions) {
        if (candidate.accountSoid == credentials.accountSoid
            && candidate.platformId == credentials.platformId
            && candidate.generation == credentials.generation
            && candidate.expiresAt > nowSeconds
            && token_matches(credentials.signOn.sessionToken,
                             candidate.signOn.sessionToken)) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool initialize_game_generation(const Credentials& credentials,
                                std::uint64_t nowSeconds,
                                GenerationInitializer initializer,
                                void* context) noexcept {
    if (initializer == nullptr || credentials.accountSoid == 0
        || credentials.platformId == 0 || credentials.generation == 0
        || credentials.issuedAt > nowSeconds || credentials.expiresAt <= nowSeconds) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    for (std::size_t index = 0; index < g_sessions.size(); ++index) {
        const auto& candidate = g_sessions[index];
        if (candidate.accountSoid != credentials.accountSoid
            || candidate.platformId != credentials.platformId
            || candidate.generation != credentials.generation
            || candidate.issuedAt > nowSeconds || candidate.expiresAt <= nowSeconds
            || !token_matches(credentials.signOn.sessionToken, candidate.signOn.sessionToken)) {
            continue;
        }
        success = g_generationInitialized[index]
                  || initializer(context, candidate.accountSoid);
        if (success) g_generationInitialized[index] = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    SecureZeroMemory(g_sessions.data(), sizeof g_sessions);
    g_generationInitialized.fill(false);
    // never reset the generation counter: teardown can happen without the process exiting and
    // realtime presence still carries the old generation, which would become authoritative
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::state::session
