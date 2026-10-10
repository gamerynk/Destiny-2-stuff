#pragma once

#include <cstdint>

#include "../../../../middleware/queuez/queuez_update.h"
#include "../../../../middleware/queuez/subscription.h"
#include "definition.h"

namespace dawn::server::bap::encrypted::queuez {

[[nodiscard]] bool valid(const SessionState& state) noexcept;

[[nodiscard]] bool stage_family4_snapshot(const SessionState& before,
                                          const middleware::queuez::Family& family,
                                          SessionState& after) noexcept;

[[nodiscard]] bool stage_family4_refresh(const SessionState& before,
                                         const middleware::queuez::Family& family,
                                         SessionState& after) noexcept;

[[nodiscard]] bool stage_family0_subscription(const SessionState& before,
                                              std::uint64_t selectedCharacter,
                                              bool& publish,
                                              bool& incremental,
                                              SessionState& after) noexcept;

[[nodiscard]] bool stage_family3_subscription(const SessionState& before,
                                              const middleware::queuez::Subscription& subscription,
                                              bool& publish,
                                              SessionState& after) noexcept;

[[nodiscard]] bool stage_change_character(const SessionState& before,
                                          ChangeCharacter& change) noexcept;

[[nodiscard]] bool stage_select_character(const SessionState& before,
                                          std::uint64_t selectedCharacterSoid,
                                          SelectCharacter& select) noexcept;

[[nodiscard]] bool stage_equipment_swap(const SessionState& before,
                                        std::uint64_t characterSoid,
                                        EquipmentSwap& swap) noexcept;

[[nodiscard]] bool stage_character_appearance_refresh(const SessionState& before,
                                                      std::uint64_t characterSoid,
                                                      CharacterAppearanceRefresh& refresh) noexcept;

[[nodiscard]] bool stage_roster_appearance_refresh(const SessionState& before,
                                                   std::uint64_t characterSoid,
                                                   bool includeRoster,
                                                   RosterAppearanceRefresh& refresh) noexcept;

[[nodiscard]] bool stage_socket_plug(const SessionState& before,
                                     std::uint64_t accountSoid,
                                     std::uint64_t characterSoid,
                                     std::uint64_t targetInstanceSoid,
                                     bool updatesAccount,
                                     SocketPlug& socketPlug) noexcept;

[[nodiscard]] bool stage_item_acquisition(const SessionState& before,
                                          std::uint64_t accountSoid,
                                          std::uint64_t characterSoid,
                                          std::uint64_t acquiredInstanceSoid,
                                          bool updatesAccount,
                                          ItemAcquisition& acquisition) noexcept;

[[nodiscard]] bool stage_profile_item_acquisition(const SessionState& before,
                                                  std::uint64_t accountSoid,
                                                  std::uint64_t acquiredInstanceSoid,
                                                  bool actionSource,
                                                  bool appended,
                                                  ProfileItemAcquisition& acquisition) noexcept;

[[nodiscard]] bool stage_item_dismantle(const SessionState& before,
                                        std::uint64_t accountSoid,
                                        std::uint64_t characterSoid,
                                        std::uint64_t dismantledInstanceSoid,
                                        bool updatesAccount,
                                        ItemDismantle& dismantle) noexcept;

[[nodiscard]] bool stage_vault_transfer(const SessionState& before,
                                        std::uint64_t accountSoid,
                                        std::uint64_t characterSoid,
                                        std::uint64_t instanceSoid,
                                        VaultTransfer& transfer) noexcept;

void stage_unsubscription(const SessionState& before,
                          std::uint8_t familyType,
                          std::uint64_t familyRootSoid,
                          SessionState& after) noexcept;

} // namespace dawn::server::bap::encrypted::queuez
