#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

#include "../../core/logging/log.h"
#include "../../core/settings/settings.h"
#include "../activity/defaults/activity_defaults_validation.h"
#include "../activity/destination/activity_destination_validation.h"
#include "../build_data/runtime.h"
#include "equipment/configured_equipment_identity.h"
#include "runtime.h"
#include "../session/session_runtime.h"
#include "../session/native_bap_endpoint.h"
#include "../activity/shared/runtime.h"
#include "state.h"
#include "storage/internal.h"

namespace dawn::state {
namespace runtime::storage {

State g_state;
SRWLOCK g_stateLock{SRWLOCK_INIT};
AccountPersistence g_accountPersistence{};
ActivityPersistence g_activityPersistence{};

thread_local const AccountRuntimeContext* g_threadAccountContext{};

AccountState& account() noexcept {
    return g_threadAccountContext != nullptr && g_threadAccountContext->account != nullptr
               ? *g_threadAccountContext->account
               : g_state.account;
}

activity::ActivityState& activity() noexcept {
    return g_threadAccountContext != nullptr && g_threadAccountContext->activity != nullptr
               ? *g_threadAccountContext->activity
               : g_state.activity;
}

AccountPersistence account_persistence() noexcept {
    return g_threadAccountContext != nullptr ? g_threadAccountContext->accountPersistence
                                             : g_accountPersistence;
}

ActivityPersistence activity_persistence() noexcept {
    return g_threadAccountContext != nullptr ? g_threadAccountContext->activityPersistence
                                             : g_activityPersistence;
}

bool publish_account_locked(const AccountState& accountState) noexcept {
    if (!account::valid(accountState)) {
        return false;
    }
    const AccountPersistence persistence = account_persistence();
    if (persistence.save != nullptr && !persistence.save(persistence.context, accountState)) {
        return false;
    }
    account() = accountState;
    return true;
}

bool publish_activity_locked(const activity::ActivityState& activityState) noexcept {
    const ActivityPersistence persistence = activity_persistence();
    if (persistence.save != nullptr) {
        // foreign participant records are only views of the active session; never resurrect them
        // after a restart without a new authenticated admission
        activity::ActivityState durable=activityState;
        for (auto& record:durable.sessions)
            if (record.sharedOwnerDetached
                || (record.sharedOwnerAccountSoid!=0 && record.sharedOwnerAccountSoid!=record.ownerAccountSoid))
                record={};
        if (!persistence.save(persistence.context, durable)) return false;
    }
    activity::ActivityState published=activityState;
    activity::shared::notify_changed_locked(activity(),published);
    activity() = published;
    return true;
}

} // namespace runtime::storage

AccountRuntimeContext capture_account_runtime(std::uint64_t& accountSoid) noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    AccountRuntimeContext context{&runtime::storage::account(),
                                  &runtime::storage::activity(),
                                  runtime::storage::account_persistence(),
                                  runtime::storage::activity_persistence()};
    accountSoid = context.account->primarySoid;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return context;
}

namespace {

std::atomic<AccountRuntimeResolver> g_accountResolver{};
std::atomic<void*> g_accountResolverContext{};
std::atomic<FriendAccountResolver> g_friendAccountResolver{};
std::atomic<void*> g_friendAccountResolverContext{};
std::atomic<FriendProfileResolver> g_friendProfileResolver{};
std::atomic<void*> g_friendProfileResolverContext{};
std::atomic<CharacterDeleteHandler> g_characterDeleteHandler{};
std::atomic<void*> g_characterDeleteContext{};
std::atomic<CharacterCreateHandler> g_characterCreateHandler{};
std::atomic<void*> g_characterCreateContext{};
std::atomic<CustomizePendingQuery> g_customizePendingQuery{};
std::atomic<void*> g_customizePendingContext{};

} // namespace

bool register_account_runtime_resolver(void* context,
                                       AccountRuntimeResolver resolver) noexcept {
    if (resolver == nullptr) {
        return false;
    }
    AccountRuntimeResolver expected = nullptr;
    if (!g_accountResolver.compare_exchange_strong(expected,
                                                   resolver,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
        return false;
    }
    g_accountResolverContext.store(context, std::memory_order_release);
    return true;
}

void unregister_account_runtime_resolver(void* context,
                                         AccountRuntimeResolver resolver) noexcept {
    if (g_accountResolver.load(std::memory_order_acquire) != resolver
        || g_accountResolverContext.load(std::memory_order_acquire) != context) {
        return;
    }
    g_accountResolverContext.store(nullptr, std::memory_order_release);
    AccountRuntimeResolver expected = resolver;
    (void)g_accountResolver.compare_exchange_strong(expected,
                                                    nullptr,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
}

bool resolve_account_runtime(std::uint64_t accountSoid,
                             std::uint64_t platformId,
                             AccountRuntimeContext& output) noexcept {
    output = {};
    const AccountRuntimeResolver resolver = g_accountResolver.load(std::memory_order_acquire);
    void* const context = g_accountResolverContext.load(std::memory_order_acquire);
    return resolver != nullptr && accountSoid != 0 && platformId != 0
           && resolver(context, accountSoid, platformId, output)
           && output.account != nullptr && output.activity != nullptr;
}

bool register_friend_account_resolver(void* context,
                                      FriendAccountResolver resolver) noexcept {
    if (resolver == nullptr) {
        return false;
    }
    FriendAccountResolver expected = nullptr;
    if (!g_friendAccountResolver.compare_exchange_strong(expected,
                                                         resolver,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
        return false;
    }
    g_friendAccountResolverContext.store(context, std::memory_order_release);
    return true;
}

void unregister_friend_account_resolver(void* context,
                                        FriendAccountResolver resolver) noexcept {
    if (g_friendAccountResolver.load(std::memory_order_acquire) != resolver
        || g_friendAccountResolverContext.load(std::memory_order_acquire) != context) {
        return;
    }
    g_friendAccountResolverContext.store(nullptr, std::memory_order_release);
    FriendAccountResolver expected = resolver;
    (void)g_friendAccountResolver.compare_exchange_strong(expected,
                                                          nullptr,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire);
}

bool resolve_friend_account(std::uint64_t viewerPlatformId,
                            std::uint64_t targetPlatformId,
                            std::uint64_t& targetAccountSoid) noexcept {
    targetAccountSoid = 0;
    const FriendAccountResolver resolver =
        g_friendAccountResolver.load(std::memory_order_acquire);
    void* const context = g_friendAccountResolverContext.load(std::memory_order_acquire);
    return resolver != nullptr && viewerPlatformId != 0 && targetPlatformId != 0
           && viewerPlatformId != targetPlatformId
           && resolver(context, viewerPlatformId, targetPlatformId, targetAccountSoid)
           && targetAccountSoid != 0;
}

bool register_friend_profile_resolver(void* context,
                                      FriendProfileResolver resolver) noexcept {
    if (resolver == nullptr) {
        return false;
    }
    FriendProfileResolver expected = nullptr;
    if (!g_friendProfileResolver.compare_exchange_strong(expected,
                                                         resolver,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
        return false;
    }
    g_friendProfileResolverContext.store(context, std::memory_order_release);
    return true;
}

void unregister_friend_profile_resolver(void* context,
                                        FriendProfileResolver resolver) noexcept {
    if (g_friendProfileResolver.load(std::memory_order_acquire) != resolver
        || g_friendProfileResolverContext.load(std::memory_order_acquire) != context) {
        return;
    }
    g_friendProfileResolverContext.store(nullptr, std::memory_order_release);
    FriendProfileResolver expected = resolver;
    (void)g_friendProfileResolver.compare_exchange_strong(expected,
                                                          nullptr,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire);
}

bool register_character_delete_handler(void* context,
                                       CharacterDeleteHandler handler) noexcept {
    if (handler == nullptr) {
        return false;
    }
    CharacterDeleteHandler expected = nullptr;
    if (!g_characterDeleteHandler.compare_exchange_strong(expected,
                                                         handler,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
        return false;
    }
    g_characterDeleteContext.store(context, std::memory_order_release);
    return true;
}

void unregister_character_delete_handler(void* context,
                                         CharacterDeleteHandler handler) noexcept {
    if (g_characterDeleteHandler.load(std::memory_order_acquire) != handler
        || g_characterDeleteContext.load(std::memory_order_acquire) != context) {
        return;
    }
    g_characterDeleteContext.store(nullptr, std::memory_order_release);
    CharacterDeleteHandler expected = handler;
    (void)g_characterDeleteHandler.compare_exchange_strong(expected,
                                                           nullptr,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire);
}

bool delete_character(std::uint64_t characterSoid) noexcept {
    const CharacterDeleteHandler handler =
        g_characterDeleteHandler.load(std::memory_order_acquire);
    void* const context = g_characterDeleteContext.load(std::memory_order_acquire);
    return handler != nullptr && characterSoid != 0 && handler(context, characterSoid);
}

bool register_character_create_handler(void* context,
                                       CharacterCreateHandler handler) noexcept {
    if (handler == nullptr) {
        return false;
    }
    CharacterCreateHandler expected = nullptr;
    if (!g_characterCreateHandler.compare_exchange_strong(expected,
                                                         handler,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
        return false;
    }
    g_characterCreateContext.store(context, std::memory_order_release);
    return true;
}

void unregister_character_create_handler(void* context,
                                         CharacterCreateHandler handler) noexcept {
    if (g_characterCreateHandler.load(std::memory_order_acquire) != handler
        || g_characterCreateContext.load(std::memory_order_acquire) != context) {
        return;
    }
    g_characterCreateContext.store(nullptr, std::memory_order_release);
    CharacterCreateHandler expected = handler;
    (void)g_characterCreateHandler.compare_exchange_strong(expected,
                                                           nullptr,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire);
}

bool create_character(const CharacterCreateRequest& request, std::uint64_t& createdSoid) noexcept {
    createdSoid = 0;
    const CharacterCreateHandler handler =
        g_characterCreateHandler.load(std::memory_order_acquire);
    void* const context = g_characterCreateContext.load(std::memory_order_acquire);
    return handler != nullptr && handler(context, request, createdSoid) && createdSoid != 0;
}

bool register_customize_pending_query(void* context, CustomizePendingQuery query) noexcept {
    if (query == nullptr) {
        return false;
    }
    CustomizePendingQuery expected = nullptr;
    if (!g_customizePendingQuery.compare_exchange_strong(expected,
                                                        query,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
        return false;
    }
    g_customizePendingContext.store(context, std::memory_order_release);
    return true;
}

bool customize_pending(std::uint64_t accountSoid) noexcept {
    const CustomizePendingQuery query = g_customizePendingQuery.load(std::memory_order_acquire);
    void* const context = g_customizePendingContext.load(std::memory_order_acquire);
    return query != nullptr && accountSoid != 0 && query(context, accountSoid);
}

bool resolve_friend_profile(std::uint64_t viewerAccountSoid,
                            std::uint64_t targetAccountSoid,
                            AccountState& output) noexcept {
    output = {};
    const FriendProfileResolver resolver =
        g_friendProfileResolver.load(std::memory_order_acquire);
    void* const context = g_friendProfileResolverContext.load(std::memory_order_acquire);
    return resolver != nullptr && viewerAccountSoid != 0 && targetAccountSoid != 0
           && viewerAccountSoid != targetAccountSoid
           && resolver(context, viewerAccountSoid, targetAccountSoid, output)
           && output.primarySoid == targetAccountSoid && account::valid(output);
}

ScopedAccountRuntime::ScopedAccountRuntime(const AccountRuntimeContext& context) noexcept
    : previous_(runtime::storage::g_threadAccountContext) {
    if (context.account != nullptr && context.activity != nullptr) {
        runtime::storage::g_threadAccountContext = &context;
    }
}

ScopedAccountRuntime::~ScopedAccountRuntime() {
    runtime::storage::g_threadAccountContext = previous_;
}

namespace {

constexpr std::uint32_t kDefaultTokenLifetimeSeconds = 3600;
constexpr std::uint64_t kGlobalFamily5Soid =
    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
template <std::size_t Size>
[[nodiscard]] bool randomize(std::array<std::byte, Size>& output) noexcept {
    return BCryptGenRandom(nullptr,
                           reinterpret_cast<PUCHAR>(output.data()),
                           static_cast<ULONG>(output.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG)
           >= 0;
}

[[nodiscard]] bool identity_uses_soid(const AccountState& accountState,
                                      std::uint64_t soid) noexcept {
    if (soid == 0 || accountState.primarySoid == soid) {
        return true;
    }
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        if (accountState.profileItems[index].instanceSoid == soid) {
            return true;
        }
    }
    for (std::size_t characterIndex = 0; characterIndex < accountState.characterCount;
         ++characterIndex) {
        const CharacterState& character = accountState.characters[characterIndex];
        if (character.soid == soid) {
            return true;
        }
        for (const std::optional<account::inventory::Item>& item : character.equipment.slots) {
            if (item.has_value() && item->instanceSoid == soid) {
                return true;
            }
        }
        for (std::size_t index = 0; index < character.inventory.count; ++index) {
            if (character.inventory.values[index].instanceSoid == soid) {
                return true;
            }
        }
    }
    for (std::size_t index = 0; index < accountState.vault.count; ++index) {
        if (accountState.vault.values[index].instanceSoid == soid) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool seed_inventory_runtime_fields(AccountState& accountState) noexcept {
    if (!account::valid_authored(accountState)) {
        return false;
    }
    for (std::size_t characterIndex = 0; characterIndex < accountState.characterCount;
         ++characterIndex) {
        CharacterState& character = accountState.characters[characterIndex];
        std::uint32_t next = 0;
        for (std::optional<account::inventory::Item>& item : character.equipment.slots) {
            if (item.has_value()) {
                item->mutationSerial = static_cast<std::int32_t>(next++);
            }
        }
        for (std::size_t index = 0; index < character.inventory.count; ++index) {
            character.inventory.values[index].mutationSerial = static_cast<std::int32_t>(next++);
        }
        character.nextInventorySerial = next;
    }
    return account::valid(accountState);
}

[[nodiscard]] bool canonicalize_profile_item_identities(AccountState& accountState) noexcept {
    if (!account::valid(accountState) || !build_data::socket_plug_rules_ready()) {
        return false;
    }
    std::array<bool, account::inventory::kProfileItemCapacity> actionSources{};
    std::size_t actionSourceCount = 0;
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        const account::inventory::ProfileItem& profileItem = accountState.profileItems[index];
        build_data::items::Definition item{};
        build_data::items::details::Definition detail{};
        build_data::inventory::buckets::Descriptor bucket{};
        if (!build_data::find_item_definition_hash(profileItem.definitionHash, item)
            || item.definitionHash != profileItem.definitionHash
            || !build_data::find_configured_item_detail(item.definitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || detail.instancedDefinitionState
                   != build_data::items::details::InstancedDefinitionState::stackable
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)
            || bucket.arraySelector != build_data::inventory::buckets::ArraySelector::profile) {
            return false;
        }
        actionSources[index] =
            build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
        if (actionSources[index]
            && ++actionSourceCount > account::inventory::kProfileActionSourceCapacity) {
            return false;
        }
    }

    // currency, material and consumable rows are non-instanced; clear any stale runtime key
    // first so it cannot reserve the action-source namespace
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        if (!actionSources[index]) {
            accountState.profileItems[index].instanceSoid = 0;
        }
    }

    std::uint64_t nextProfileSoid = account::inventory::kFirstProfileItemInstanceSoid;
    for (std::size_t index = 0; index < accountState.profileItemCount; ++index) {
        account::inventory::ProfileItem& item = accountState.profileItems[index];
        if (!actionSources[index] || item.instanceSoid != 0) {
            continue;
        }
        while (identity_uses_soid(accountState, nextProfileSoid)) {
            if (nextProfileSoid == (std::numeric_limits<std::uint64_t>::max)()) {
                return false;
            }
            ++nextProfileSoid;
        }
        item.instanceSoid = nextProfileSoid;
        if (nextProfileSoid != (std::numeric_limits<std::uint64_t>::max)()) {
            ++nextProfileSoid;
        }
    }
    return account::valid(accountState);
}

} // namespace

bool prepare_account_runtime(AccountState& accountState) noexcept {
    if (!seed_inventory_runtime_fields(accountState)) {
        return false;
    }
    return !build_data::socket_plug_rules_ready()
           || canonicalize_profile_item_identities(accountState);
}

bool initialize(void* module, const AccountState& initialAccount) noexcept {
    return initialize(module, initialAccount, activity::defaults::authored(), {});
}

[[nodiscard]] bool valid_restored_activity(const activity::ActivityState& value,
                                           std::uint64_t accountSoid) noexcept {
    if (accountSoid == 0 || value.stateRevision == activity::kInvalidRevision
        || value.allocatorRevision == activity::kInvalidRevision
        || value.nextSessionId == activity::kAbsentSessionId) {
        return false;
    }
    for (std::size_t index = 0; index < value.sessions.size(); ++index) {
        const activity::SessionRecord& record = value.sessions[index];
        if (!record.occupied) {
            continue;
        }
        if (record.sessionId == activity::kAbsentSessionId
            || record.ownerAccountSoid != accountSoid
            || record.createdRevision == activity::kInvalidRevision
            || record.recordRevision == activity::kInvalidRevision
            || record.createdRevision > record.recordRevision
            || record.recordRevision > value.stateRevision
            || !activity::destination::valid(record.destination)
            || (record.joined
                != (record.joinedRevision != activity::kInvalidRevision))) {
            return false;
        }
        for (std::size_t other = index + 1; other < value.sessions.size(); ++other) {
            if (value.sessions[other].occupied
                && value.sessions[other].sessionId == record.sessionId) {
                return false;
            }
        }
    }
    return true;
}

bool initialize(void* module,
                const AccountState& initialAccount,
                const activity::defaults::ActivityDefaults& activityDefaults,
                const AccountRuntimeOptions& options) noexcept {
    AccountState runtimeAccount = initialAccount;
    const bool accountReady = options.preserveRuntimeFields
                                  ? account::valid(runtimeAccount)
                                  : seed_inventory_runtime_fields(runtimeAccount);
    if (!accountReady
        || !activity::defaults::valid(activityDefaults)) {
        return false;
    }
    const std::uint64_t buildDataEquipmentHash =
        options.hasBuildDataEquipmentHash
            ? options.buildDataEquipmentHash
            : runtime::equipment::configured_hash(runtimeAccount);
    if (!build_data::initialize(module, buildDataEquipmentHash)) {
        return false;
    }
    // a cache hit already has the plug relation, so publish canonical profile identities in
    // the first image; on a first cache build snapshot preparation repeats it after extraction
    if (build_data::socket_plug_rules_ready()
        && !canonicalize_profile_item_identities(runtimeAccount)) {
        build_data::shutdown();
        return false;
    }
    {
        std::array<char, 96> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=account stage=identity primary=0x%016llX characters=%zu",
                          static_cast<unsigned long long>(runtimeAccount.primarySoid),
                          runtimeAccount.characterCount);
        if (written > 0) {
            core::log::write(core::log::Channel::state,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    State initialized{};
    if (!randomize(initialized.signOn.encryptionKey)
        || !randomize(initialized.signOn.authenticationKey)
        || !randomize(initialized.signOn.sessionToken) || !randomize(initialized.bap.nonce)
        || !randomize(initialized.bap.sessionKey) || !randomize(initialized.bap.envelopeIv)) {
        SecureZeroMemory(&initialized, sizeof initialized);
        build_data::shutdown();
        return false;
    }
    session::publish_native_bap_endpoint(initialized.signOn, core::settings::get());
    initialized.signOn.tokenLifetimeSeconds = kDefaultTokenLifetimeSeconds;
    initialized.account = runtimeAccount;
    if (options.restoreActivity) {
        if (!valid_restored_activity(options.restoredActivity, runtimeAccount.primarySoid)) {
            SecureZeroMemory(&initialized, sizeof initialized);
            build_data::shutdown();
            return false;
        }
        initialized.activity = options.restoredActivity;
        // no live activity connections yet: keep resumable data but do not treat previous-process
        // joined bits as live session pins
        for (auto& record : initialized.activity.sessions)
            record.restoredDormant = record.occupied && record.sharedOwnerAccountSoid == 0;
        initialized.activity.defaults = activityDefaults;
    } else {
        initialized.activity.defaults = activityDefaults;
    }
    initialized.investment.family5.objectSoid = kGlobalFamily5Soid;
    // only the override lists come from settings; identity and gate stay owned by State
    const Family5State& authored = core::settings::get().initialFamily5;
    initialized.investment.family5.flags = authored.flags;
    initialized.investment.family5.flagCount = authored.flagCount;
    initialized.investment.family5.values = authored.values;
    initialized.investment.family5.valueCount = authored.valueCount;
    // the arm is account-wide and rides the first ws-503, before any pick, so arm it when any
    // authored character carries the bypass; the per-character objB byte picks which opens
    for (std::size_t index = 0; index < runtimeAccount.characterCount; ++index) {
        if (runtimeAccount.characters[index].contentBypass) {
            initialized.investment.family5.contentGateArm = true;
            break;
        }
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    activity::shared::reset_locked();
    runtime::storage::g_state = initialized;
    runtime::storage::g_accountPersistence = options.persistence;
    runtime::storage::g_activityPersistence = options.activityPersistence;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    SecureZeroMemory(&initialized, sizeof initialized);
    return true;
}

void shutdown() noexcept {
    session::shutdown();
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    activity::shared::reset_locked();
    SecureZeroMemory(&runtime::storage::g_state, sizeof runtime::storage::g_state);
    runtime::storage::g_accountPersistence = {};
    runtime::storage::g_activityPersistence = {};
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    build_data::shutdown();
}

const SignOnState& sign_on() noexcept {
    return runtime::storage::g_state.signOn;
}

bool ensure_profile_item_identities() noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::account();
    const bool ready = canonicalize_profile_item_identities(candidate);
    const bool published = ready && runtime::storage::publish_account_locked(candidate);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return published;
}

bool publish_bootstrap_token(std::span<const std::byte> token) noexcept {
    if (token.size() != runtime::storage::g_state.signOn.bootstrapToken.size()) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SignOnState& signOn = runtime::storage::g_state.signOn;
    std::copy(token.begin(), token.end(), signOn.bootstrapToken.begin());
    signOn.bootstrapTokenPresent = true;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

bool bootstrap_token_present() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const bool present = runtime::storage::g_state.signOn.bootstrapTokenPresent;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return present;
}

const BapState& bap() noexcept {
    return runtime::storage::g_state.bap;
}

InvestmentState investment_snapshot() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const InvestmentState snapshot = runtime::storage::g_state.investment;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return snapshot;
}

} // namespace dawn::state
