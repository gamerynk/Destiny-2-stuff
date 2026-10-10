#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "state.h"

namespace dawn::state {

// backing storage must outlive the deferred work. the process owner is returned
// explicitly so a later unrelated TLS scope cannot win
[[nodiscard]] AccountRuntimeContext capture_account_runtime(std::uint64_t& accountSoid) noexcept;

[[nodiscard]] bool register_account_runtime_resolver(void* context,
                                                     AccountRuntimeResolver resolver) noexcept;

void unregister_account_runtime_resolver(void* context,
                                         AccountRuntimeResolver resolver) noexcept;

[[nodiscard]] bool resolve_account_runtime(std::uint64_t accountSoid,
                                           std::uint64_t platformId,
                                           AccountRuntimeContext& output) noexcept;

[[nodiscard]] bool register_friend_account_resolver(void* context,
                                                    FriendAccountResolver resolver) noexcept;

void unregister_friend_account_resolver(void* context,
                                        FriendAccountResolver resolver) noexcept;

using CharacterDeleteHandler = bool (*)(void*, std::uint64_t) noexcept;

[[nodiscard]] bool register_character_delete_handler(void* context,
                                                     CharacterDeleteHandler handler) noexcept;

void unregister_character_delete_handler(void* context,
                                         CharacterDeleteHandler handler) noexcept;

[[nodiscard]] bool delete_character(std::uint64_t characterSoid) noexcept;

struct CharacterCreateRequest {
    // zero is the admin form: the first runtime account
    std::uint64_t accountSoid{};
    std::uint8_t characterClass{};
    std::uint8_t race{};
    std::uint8_t gender{};
    std::array<std::uint8_t, 36> identityHeader{};
    bool hasIdentityHeader{};
};

using CharacterCreateHandler =
    bool (*)(void*, const CharacterCreateRequest&, std::uint64_t&) noexcept;

[[nodiscard]] bool register_character_create_handler(void* context,
                                                     CharacterCreateHandler handler) noexcept;

void unregister_character_create_handler(void* context,
                                         CharacterCreateHandler handler) noexcept;

[[nodiscard]] bool create_character(const CharacterCreateRequest& request,
                                    std::uint64_t& createdSoid) noexcept;

using CustomizePendingQuery = bool (*)(void*, std::uint64_t accountSoid) noexcept;

[[nodiscard]] bool register_customize_pending_query(void* context,
                                                    CustomizePendingQuery query) noexcept;

// always false when no query is installed (embedded client)
[[nodiscard]] bool customize_pending(std::uint64_t accountSoid) noexcept;

[[nodiscard]] bool resolve_friend_account(std::uint64_t viewerPlatformId,
                                          std::uint64_t targetPlatformId,
                                          std::uint64_t& targetAccountSoid) noexcept;

[[nodiscard]] bool register_friend_profile_resolver(void* context,
                                                    FriendProfileResolver resolver) noexcept;

void unregister_friend_profile_resolver(void* context,
                                        FriendProfileResolver resolver) noexcept;

[[nodiscard]] bool resolve_friend_profile(std::uint64_t viewerAccountSoid,
                                          std::uint64_t targetAccountSoid,
                                          AccountState& output) noexcept;

class ScopedAccountRuntime final {
public:
    explicit ScopedAccountRuntime(const AccountRuntimeContext& context) noexcept;
    ~ScopedAccountRuntime();
    ScopedAccountRuntime(const ScopedAccountRuntime&) = delete;
    ScopedAccountRuntime& operator=(const ScopedAccountRuntime&) = delete;

private:
    const AccountRuntimeContext* previous_{};
};

[[nodiscard]] bool prepare_account_runtime(AccountState& account) noexcept;

// only profile mod/shader rows that are socket action sources get SOIDs; currency,
// material and consumable rows stay non-instanced
[[nodiscard]] bool ensure_profile_item_identities() noexcept;

enum class EmoteCollectionOutcome : std::uint8_t {
    ready,
    notReady,
    unsupported,
    failed,
};

// equips the "Emotes" collection item (hash 3183180185) in the emote slot so the stock
// client opens its wheel config. idempotent; repairs a broken copy
[[nodiscard]] EmoteCollectionOutcome ensure_character_emote_collection() noexcept;


enum class EquipmentMutationKind : std::uint8_t {
    none,
    equip,
    unequip,
};

struct PendingEquipmentSwap {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::uint64_t characterSoid{};
    std::uint64_t requestedInstanceSoid{};
    std::uint64_t previousInstanceSoid{};
    std::size_t characterIndex{};
    std::size_t equipmentSlotIndex{};
    std::size_t inventoryIndex{};
    std::size_t movedItemCount{};
    std::uint8_t nativeEquipmentSlot{};
    EquipmentMutationKind kind{};
    bool prepared{};
};

struct PendingItemAcquisition {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t acquiredInstanceSoid{};
    std::uint32_t acquiredDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::uint32_t expectedNextInventorySerial{};
    std::size_t characterIndex{};
    std::size_t expectedInventoryCount{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    std::size_t inventoryIndex{};
    std::uint16_t collectibleIndex{};
    std::uint16_t inventoryRow{};
    std::uint8_t equipmentSlot{};
    std::uint8_t materialRequirementCount{};
    bool profileChanged{};
    bool prepared{};
};

struct PendingProfileItemAcquisition {
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterItems{};
    std::uint64_t accountSoid{};
    std::uint64_t acquiredInstanceSoid{};
    std::uint32_t acquiredDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::size_t expectedItemCount{};
    std::size_t afterItemCount{};
    std::size_t profileIndex{};
    std::int32_t previousQuantity{};
    std::int32_t acquiredQuantity{};
    std::int32_t previousMutationSerial{};
    std::int32_t acquiredMutationSerial{};
    std::uint16_t collectibleIndex{};
    std::uint8_t bucketId{};
    std::uint8_t materialRequirementCount{};
    bool actionSource{};
    bool appended{};
    bool prepared{};
};

struct DismantleReward {
    std::uint32_t definitionHash{};
    std::size_t profileIndex{};
    std::int32_t quantity{};
    std::int32_t afterQuantity{};
    std::int32_t mutationSerial{};
};

inline constexpr std::size_t kDismantleRewardCapacity = kDismantleRewardPolicyCapacity;

struct PendingItemDismantle {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::array<DismantleReward, kDismantleRewardCapacity> rewards{};
    account::inventory::Item dismantledItem{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t dismantledInstanceSoid{};
    std::size_t characterIndex{};
    std::size_t expectedInventoryCount{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    std::size_t inventoryIndex{};
    std::size_t movedInventoryItemCount{};
    std::size_t rewardCount{};
    std::uint16_t inventoryRow{};
    std::uint8_t equipmentSlot{};
    bool profileChanged{};
    bool prepared{};
};

// holds both whole account images, so it only lives in per-thread outcome storage
struct PendingVaultTransfer {
    AccountState before{};
    AccountState after{};
    account::inventory::Item movedItem{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t instanceSoid{};
    std::size_t characterIndex{};
    bool toVault{};
    bool prepared{};
};

struct PendingSocketPlug {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t targetInstanceSoid{};
    std::uint32_t targetDefinitionHash{};
    std::uint32_t plugDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::size_t characterIndex{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    std::size_t itemIndex{};
    std::uint16_t targetDefinitionIndex{};
    std::uint16_t plugDefinitionIndex{};
    std::uint16_t materialRequirementSetIndex{0xFFFFU};
    std::uint8_t socketLane{};
    std::uint8_t targetBucketId{};
    std::uint8_t plugBucketId{};
    std::uint8_t materialRequirementCount{};
    bool profileChanged{};
    bool targetEquipped{};
    bool prepared{};
};

struct PendingItemState {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::uint64_t characterSoid{};
    std::uint64_t targetInstanceSoid{};
    std::size_t characterIndex{};
    std::size_t itemIndex{};
    std::uint16_t targetDefinitionIndex{};
    std::uint32_t beforeFlags{};
    std::uint32_t afterFlags{};
    bool targetEquipped{};
    bool prepared{};
};

struct PendingAbilitySelection {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::uint64_t characterSoid{};
    std::uint64_t subclassInstanceSoid{};
    std::size_t characterIndex{};
    std::uint8_t socketEntry{};
    bool prepared{};
};

[[nodiscard]] bool initialize(void* module = nullptr,
                              const AccountState& initialAccount = {}) noexcept;

[[nodiscard]] bool
initialize(void* module,
           const AccountState& initialAccount,
           const activity::defaults::ActivityDefaults& activityDefaults,
           const AccountRuntimeOptions& options = {}) noexcept;

void shutdown() noexcept;

[[nodiscard]] const SignOnState& sign_on() noexcept;

[[nodiscard]] bool publish_bootstrap_token(std::span<const std::byte> token) noexcept;

[[nodiscard]] bool bootstrap_token_present() noexcept;

[[nodiscard]] const BapState& bap() noexcept;

[[nodiscard]] bool set_primary_soid(std::uint64_t primarySoid) noexcept;

// the client names its pick only in the select-character request, so this is where it
// enters State
[[nodiscard]] bool set_selected_character(std::uint64_t characterSoid, bool& changed) noexcept;

// called once by serialized BAP hello before initial Family4 publication; leaves
// equipment, inventory and identity alone
[[nodiscard]] bool begin_character_selection(std::uint64_t authenticatedAccountSoid) noexcept;

[[nodiscard]] bool prepare_equipment_swap(std::uint64_t requestedInstanceSoid,
                                          PendingEquipmentSwap& mutation) noexcept;

// inserted before existing items in its native bucket so their published rows stay stable
[[nodiscard]] bool prepare_equipment_unequip(std::uint64_t requestedInstanceSoid,
                                             PendingEquipmentSwap& mutation) noexcept;

[[nodiscard]] bool commit_equipment_swap(PendingEquipmentSwap& mutation) noexcept;

[[nodiscard]] bool prepare_item_acquisition(std::uint16_t collectibleIndex,
                                            std::uint32_t definitionHash,
                                            PendingItemAcquisition& mutation) noexcept;

[[nodiscard]] bool preview_item_acquisition(const PendingItemAcquisition& mutation,
                                            AccountState& after) noexcept;

[[nodiscard]] bool commit_item_acquisition(PendingItemAcquisition& mutation) noexcept;

[[nodiscard]] bool
prepare_profile_item_acquisition(std::uint16_t collectibleIndex,
                                 std::uint32_t definitionHash,
                                 PendingProfileItemAcquisition& mutation) noexcept;

[[nodiscard]] bool preview_profile_item_acquisition(const PendingProfileItemAcquisition& mutation,
                                                    AccountState& after) noexcept;

[[nodiscard]] bool
commit_profile_item_acquisition(PendingProfileItemAcquisition& mutation) noexcept;

[[nodiscard]] bool prepare_item_dismantle(std::uint64_t instanceSoid,
                                          PendingItemDismantle& mutation) noexcept;

[[nodiscard]] bool preview_item_dismantle(const PendingItemDismantle& mutation,
                                          AccountState& after) noexcept;

[[nodiscard]] bool commit_item_dismantle(PendingItemDismantle& mutation) noexcept;

[[nodiscard]] bool prepare_socket_plug(std::uint64_t targetInstanceSoid,
                                       std::uint8_t socketLane,
                                       std::uint16_t plugDefinitionIndex,
                                       PendingSocketPlug& mutation) noexcept;

[[nodiscard]] bool prepare_character_selector_socket_plug(std::uint64_t instanceIdentityToken,
                                                          std::uint8_t requestedSocketLane,
                                                          std::uint16_t plugDefinitionIndex,
                                                          PendingSocketPlug& mutation) noexcept;

[[nodiscard]] bool preview_socket_plug(const PendingSocketPlug& mutation,
                                       AccountState& after) noexcept;

[[nodiscard]] bool commit_socket_plug(PendingSocketPlug& mutation) noexcept;

[[nodiscard]] bool prepare_item_state(std::uint64_t targetInstanceSoid,
                                      std::uint16_t targetDefinitionIndex,
                                      std::uint32_t flags,
                                      PendingItemState& mutation) noexcept;

[[nodiscard]] bool commit_item_state(PendingItemState& mutation) noexcept;

// class ability, movement and grenade entries replace that choice; a tree entry moves the
// super and melee. a pick with no ability row is refused here, not while building
[[nodiscard]] bool prepare_ability_selection(std::uint64_t subclassInstanceSoid,
                                             std::uint8_t socketEntry,
                                             PendingAbilitySelection& mutation) noexcept;

[[nodiscard]] bool commit_ability_selection(PendingAbilitySelection& mutation) noexcept;

[[nodiscard]] bool prepare_vault_transfer(std::uint64_t instanceSoid,
                                          PendingVaultTransfer& mutation) noexcept;

[[nodiscard]] bool preview_vault_transfer(const PendingVaultTransfer& mutation,
                                          AccountState& after) noexcept;

[[nodiscard]] bool commit_vault_transfer(PendingVaultTransfer& mutation) noexcept;

[[nodiscard]] AccountState account_snapshot() noexcept;

[[nodiscard]] InvestmentState investment_snapshot() noexcept;

} // namespace dawn::state
