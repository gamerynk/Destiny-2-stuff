#pragma once

#include "../../../../../middleware/datagen/family4/loadout/definition.h"
#include "../../../../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../../../../state/account/account_state.h"
#include "../../../../../state/build_data/definition.h"
#include "../../../../../state/equipment/light/definition.h"
#include "snapshot.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {

inline constexpr std::int32_t kInitialFamilyVersion = 0;
inline constexpr std::uint32_t kPublicProfileFamilyType = 1;
inline constexpr std::uint32_t kSocialRosterFamilyType = 2;
inline constexpr std::uint32_t kRosterFamilyType = 3;
inline constexpr std::uint32_t kAccountFamilyType = 4;
inline constexpr std::uint32_t kFireteamFamilyType = 6;
inline constexpr std::uint32_t kJoinFamilyType = 7;
inline constexpr std::uint32_t kRosterDefinitionSlotIndex = 0;
inline constexpr std::uint32_t kAccountDefinitionSlotIndex = 0;
inline constexpr std::uint32_t kCharacterDefinitionSlotIndex = 1;
inline constexpr std::uint32_t kItemDefinitionSlotIndex = 3;
inline constexpr std::size_t kRosterObjectIndex = 0;
inline constexpr std::size_t kAccountObjectIndex = 0;
inline constexpr std::size_t kCharacterObjectIndex = 1;
inline constexpr std::size_t kFirstItemObjectIndex = kFamily4IdentityObjectCount;
inline constexpr std::size_t kFirstItemObjectIndexUnselected = kAccountObjectIndex + 1;
inline constexpr std::size_t kSingleObjectCount = 1;

[[nodiscard]] bool prepare_public_profile(Scratch& scratch,
                                          const middleware::queuez::Subscription& subscription,
                                          std::uint32_t rootObjectId,
                                          const Reservation& reservation,
                                          Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_roster(Scratch& scratch,
                                  const middleware::queuez::Subscription& subscription,
                                  std::uint32_t objectId,
                                  const Reservation& reservation,
                                  Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_social_roster(Scratch& scratch,
                                         const middleware::queuez::Subscription& subscription,
                                         std::uint32_t objectId,
                                         const Reservation& reservation,
                                         Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_fireteam(Scratch& scratch,
                                    const middleware::queuez::Subscription& subscription,
                                    const Reservation& reservation,
                                    Prepared& prepared,
                                    const state::social::RemoteAccount* liveRemote,
                                    std::uint64_t authenticatedSelfRoot) noexcept;

[[nodiscard]] bool prepare_join(Scratch& scratch,
                               const middleware::queuez::Subscription& subscription,
                               const Reservation& reservation,
                               Prepared& prepared,
                               const state::social::RemoteAccount* liveRemote) noexcept;

[[nodiscard]] bool compress_object(Scratch& scratch,
                                   std::span<const std::byte> encoded,
                                   std::uint32_t definitionId,
                                   std::uint64_t version,
                                   std::size_t destinationOffset,
                                   middleware::queuez::Object& object,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool prepare(Scratch& scratch,
                           const middleware::queuez::Subscription& subscription,
                           std::uint32_t accountObjectId,
                           const Reservation& reservation,
                           Prepared& prepared,
                           std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions = {}) noexcept;

[[nodiscard]] bool prepare_selection_move(Scratch& scratch,
                                          const queuez::SelectCharacter& select,
                                          Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_equipment_swap(Scratch& scratch,
                                          const queuez::EquipmentSwap& swap,
                                          const state::PendingEquipmentSwap& mutation,
                                          Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_item_state(Scratch& scratch,
                                      const queuez::EquipmentSwap& update,
                                      const state::PendingItemState& mutation,
                                      Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_socket_plug(Scratch& scratch,
                                       const queuez::SocketPlug& socketPlug,
                                       const state::PendingSocketPlug& mutation,
                                       Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_item_acquisition(Scratch& scratch,
                                            const queuez::ItemAcquisition& acquisition,
                                            const state::PendingItemAcquisition& mutation,
                                            Prepared& prepared) noexcept;

[[nodiscard]] bool
prepare_profile_item_acquisition(Scratch& scratch,
                                 const queuez::ProfileItemAcquisition& acquisition,
                                 const state::PendingProfileItemAcquisition& mutation,
                                 Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_item_dismantle(Scratch& scratch,
                                          const queuez::ItemDismantle& dismantle,
                                          const state::PendingItemDismantle& mutation,
                                          Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_vault_transfer(Scratch& scratch,
                                          const queuez::VaultTransfer& transfer,
                                          const state::PendingVaultTransfer& mutation,
                                          Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_ability_selection(Scratch& scratch,
                                             const queuez::EquipmentSwap& update,
                                             const state::PendingAbilitySelection& mutation,
                                             Prepared& prepared) noexcept;

struct Resolved {
    std::size_t characterIndex{};
    middleware::datagen::family4::loadout::ResolvedLoadout loadout{};
    state::equipment::light::Evaluation lightEvaluation{};
    std::uint32_t characterObjectId{};
    std::uint32_t itemInstanceObjectId{};
};

[[nodiscard]] std::optional<std::size_t>
find_character_index(const state::AccountState& account) noexcept;

[[nodiscard]] bool
resolve(const state::AccountState& account, std::size_t characterIndex, Resolved& output) noexcept;

[[nodiscard]] bool report_failure(const char* step) noexcept;

[[nodiscard]] bool append_object(Scratch& scratch,
                                 std::span<const std::byte> encoded,
                                 std::uint32_t definitionId,
                                 std::uint64_t version,
                                 middleware::queuez::Object& object,
                                 std::size_t& compressedExtent) noexcept;

[[nodiscard]] bool
append_items(Scratch& scratch,
             std::span<std::byte> rawStorage,
             std::uint32_t itemInstanceObjectId,
             const middleware::datagen::family4::loadout::ResolvedInstances& instances,
             std::size_t baseIndex,
             Prepared& staged,
             std::size_t& itemCursor,
             std::size_t& compressedExtent) noexcept;

[[nodiscard]] bool resolve_profile_item_instance(
    const state::account::inventory::ProfileItem& profileItem,
    middleware::datagen::family4::instance::ResolvedInstance& output) noexcept;

[[nodiscard]] bool append_vault_items(Scratch& scratch,
                                      std::span<std::byte> rawStorage,
                                      std::uint32_t itemInstanceObjectId,
                                      const state::AccountState& account,
                                      std::size_t baseIndex,
                                      Prepared& staged,
                                      std::size_t& itemCursor,
                                      std::size_t& compressedExtent) noexcept;

[[nodiscard]] bool append_profile_items(Scratch& scratch,
                                        std::span<std::byte> rawStorage,
                                        std::uint32_t itemInstanceObjectId,
                                        const state::AccountState& account,
                                        std::size_t baseIndex,
                                        Prepared& staged,
                                        std::size_t& itemCursor,
                                        std::size_t& compressedExtent) noexcept;

} // namespace dawn::server::bap::encrypted::push::snapshot
