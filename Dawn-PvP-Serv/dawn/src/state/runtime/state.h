#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../account/account_state.h"
#include "../activity/definition.h"
#include "../investment/investment.h"
#include "../matchmaking/definition.h"

namespace dawn::state {

struct AccountPersistence {
    using Save = bool (*)(void* context, const AccountState& account) noexcept;

    void* context{};
    Save save{};
};

struct ActivityPersistence {
    using Save = bool (*)(void* context, const activity::ActivityState& activity) noexcept;

    void* context{};
    Save save{};
};

struct AccountRuntimeContext {
    AccountState* account{};
    activity::ActivityState* activity{};
    AccountPersistence accountPersistence{};
    ActivityPersistence activityPersistence{};
};

using AccountRuntimeResolver = bool (*)(void* context,
                                        std::uint64_t accountSoid,
                                        std::uint64_t platformId,
                                        AccountRuntimeContext& output) noexcept;

using FriendAccountResolver = bool (*)(void* context,
                                       std::uint64_t viewerPlatformId,
                                       std::uint64_t targetPlatformId,
                                       std::uint64_t& targetAccountSoid) noexcept;

using FriendProfileResolver = bool (*)(void* context,
                                       std::uint64_t viewerAccountSoid,
                                       std::uint64_t targetAccountSoid,
                                       AccountState& output) noexcept;

struct AccountRuntimeOptions {
    AccountPersistence persistence{};
    bool preserveRuntimeFields{};
    // fingerprint of the authored equipment, used to identify the build-data cache; a server
    // restoring mutable gear must supply it from its settings seed, not the durable account
    std::uint64_t buildDataEquipmentHash{};
    bool hasBuildDataEquipmentHash{};
    ActivityPersistence activityPersistence{};
    activity::ActivityState restoredActivity{};
    bool restoreActivity{};
};

inline constexpr std::size_t kAesKeySize = 16;
inline constexpr std::size_t kSessionTokenSize = 32;
inline constexpr std::size_t kBootstrapTokenSize = 16;
inline constexpr std::size_t kBapNonceSize = 12;
inline constexpr std::size_t kEnvelopeIvSize = 16;

struct SignOnState {
    std::array<std::byte, kAesKeySize> encryptionKey{};
    std::array<std::byte, kAesKeySize> authenticationKey{};
    std::array<std::byte, kSessionTokenSize> sessionToken{};
    std::uint32_t relayAddress{};
    std::uint16_t relayPort{};
    std::uint32_t tokenLifetimeSeconds{};
    // read from the installed client at activation; never bundled, saved or logged
    std::array<std::byte, kBootstrapTokenSize> bootstrapToken{};
    bool bootstrapTokenPresent{};
};

struct BapState {
    std::array<std::byte, kBapNonceSize> nonce{};
    std::array<std::byte, kAesKeySize> sessionKey{};
    std::array<std::byte, kEnvelopeIvSize> envelopeIv{};
};

struct State {
    SignOnState signOn;
    BapState bap;
    AccountState account;
    activity::ActivityState activity;
    InvestmentState investment;
    matchmaking::MatchmakingState matchmaking;
};

} // namespace dawn::state
