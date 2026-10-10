#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include "../../../middleware/bap/family_unsubscription.h"
#include "../../../middleware/bap/frame.h"
#include "../../../middleware/bap/activity_message/request_provenance.h"
#include "../../../middleware/queuez/subscription.h"
#include "../../../state/vendors/answered_interactions.h"
#include "../../web_service/web_service_runtime.h"
#include "../internal.h"
#include "activity_message/definition.h"
#include "queuez/definition.h"
#include "transactions/definition.h"

namespace dawn::server::bap::encrypted {
[[nodiscard]] bool weapon_acquisition_push_self_test(std::uint64_t characterSoid,
    std::uint64_t instanceSoid, void* compression) noexcept;

enum class BodyCodec : std::uint8_t {
    empty,
    accountTranslationResponse,
    activityHostManagerResponse,
    activityMessageRequest,
    activityHostResponse,
    clientConfigResponse,
    familySubscription,
    familyUnsubscription,
    matchmakingResponse,
    steamCertificate,
    userMessageResponse,
    webService,
};

struct EquipmentSwapTransaction {
    state::PendingEquipmentSwap pending{};
    queuez::EquipmentSwap update{};
};

struct SocketPlugTransaction {
    state::PendingSocketPlug pending{};
    queuez::SocketPlug update{};
};

struct ItemStateTransaction {
    state::PendingItemState pending{};
    queuez::EquipmentSwap update{};
};

struct ItemAcquisitionTransaction {
    state::PendingItemAcquisition pending{};
    queuez::ItemAcquisition update{};
    std::uint16_t answeredVendor{state::vendors::kAbsentIndex};
};

struct ProfileItemAcquisitionTransaction {
    state::PendingProfileItemAcquisition pending{};
    queuez::ProfileItemAcquisition update{};
    std::uint16_t answeredVendor{state::vendors::kAbsentIndex};
};

struct ItemDismantleTransaction {
    state::PendingItemDismantle pending{};
    queuez::ItemDismantle update{};
};

struct VaultTransferTransaction {
    state::PendingVaultTransfer pending{};
    queuez::VaultTransfer update{};
};

struct AbilitySelectionTransaction {
    state::PendingAbilitySelection pending{};
    queuez::EquipmentSwap update{};
};

struct InlineSubscriptionUpdate {
    std::span<std::byte> bytes{};
    std::size_t size{};
};

struct ServiceOutcome {
    std::uint16_t responseStatus{200};
    bool hasSubscription{};
    middleware::queuez::Subscription subscription{};
    bool hasUnsubscription{};
    middleware::bap::family_unsubscription::Request unsubscription{};
    bool hasChangeCharacter{};
    queuez::ChangeCharacter changeCharacter{};
    bool hasSelectCharacter{};
    queuez::SelectCharacter selectCharacter{};
    using Transaction = std::variant<std::monostate,
                                     state::activity::PendingAllocation,
                                     activity_message::ActivityPlan,
                                     state::matchmaking::PendingMutation,
                                     EquipmentSwapTransaction,
                                     SocketPlugTransaction,
                                     ItemStateTransaction,
                                     ItemAcquisitionTransaction,
                                     ProfileItemAcquisitionTransaction,
                                     ItemDismantleTransaction,
                                     VaultTransferTransaction,
                                     AbilitySelectionTransaction>;
    Transaction transaction{};
};

inline void reset(ServiceOutcome& outcome) noexcept {
    outcome.responseStatus = 200;
    outcome.hasSubscription = false;
    outcome.subscription = {};
    outcome.hasUnsubscription = false;
    outcome.unsubscription = {};
    outcome.hasChangeCharacter = false;
    outcome.changeCharacter = {};
    outcome.hasSelectCharacter = false;
    outcome.selectCharacter = {};
    outcome.transaction.emplace<std::monostate>();
}

template <typename Transaction>
[[nodiscard]] Transaction* transaction_if(ServiceOutcome& outcome) noexcept {
    return std::get_if<Transaction>(&outcome.transaction);
}

template <typename Transaction>
[[nodiscard]] const Transaction* transaction_if(const ServiceOutcome& outcome) noexcept {
    return std::get_if<Transaction>(&outcome.transaction);
}

enum class ResponseMode : std::uint8_t {
    none,
    reply,
    uncorrelatedPush,
};

struct ServiceRoute {
    ResponseMode responseMode{};
    middleware::bap::ResponseService response{};
    BodyCodec bodyCodec{};
    std::string_view successEvent{};
};

namespace routing {

[[nodiscard]] bool resolve(std::uint16_t request, ServiceRoute& route) noexcept;

} // namespace routing

namespace diagnostics {

void report_failure(std::uint16_t service, std::string_view stage) noexcept;

} // namespace diagnostics

namespace reply {

[[nodiscard]] bool encode(Scratch& scratch,
                          const ServiceRoute& route,
                          std::uint32_t taskId,
                          std::span<const std::byte, state::kAesKeySize> key,
                          std::span<const std::byte, state::kBapNonceSize> nonce,
                          std::span<const std::byte> body,
                          std::span<std::byte> response,
                          std::size_t& written,
                          std::uint16_t status = 200) noexcept;

} // namespace reply

namespace body {

[[nodiscard]] bool process(const ServiceRoute& route,
                           const queuez::SessionState& queuezState,
                           std::uint64_t activitySessionId,
                           state::matchmaking::ContextHandle matchmakingContext,
                           std::uint64_t accountSoid,
                           std::uint64_t platformId,
                           std::span<const std::byte> requestBody,
                           std::span<std::byte> output,
                           std::size_t& written,
                           ServiceOutcome& outcome,
                           std::uint64_t activityJoinRevision=0,
                           const middleware::bap::activity_message::patch_epoch::PatchEpoch* expectedEpoch=nullptr,
                           const middleware::bap::activity_message::RequestProvenance& provenance={}) noexcept;

} // namespace body

namespace push {

void ensure_account_canonical() noexcept;

// an unbuildable snapshot is skipped; the subscribe is still answered since an unanswered request kills the link
void append_queuez_notification(Scratch& scratch,
                                const queuez::SessionState& before,
                                const middleware::queuez::Subscription& subscription,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written,
                                queuez::SessionState& after,
                                bool& armsRepush,
                                bool& armsBannerRepush,
                                remote_appearance::Published* foreignAppearance = nullptr,
                                InlineSubscriptionUpdate* inlineUpdate = nullptr) noexcept;

[[nodiscard]] bool append_remote_appearance_notification(Scratch& scratch,
    remote_appearance::Published before,
    std::span<const std::byte,state::kAesKeySize> key,
    std::array<std::byte,state::kBapNonceSize>& nonce,
    std::span<std::byte> response,std::size_t& written,
    remote_appearance::Published& after) noexcept;

[[nodiscard]] bool
append_account_resync_notification(Scratch& scratch,
                                   const queuez::SessionState& before,
                                   std::span<const std::byte, state::kAesKeySize> key,
                                   std::array<std::byte, state::kBapNonceSize>& nonce,
                                   std::span<std::byte> response,
                                   std::size_t& written,
                                   queuez::SessionState& after,
                                   std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions = {}) noexcept;

// sent twice per boot at the same version, which survives the state-1 DECLARED race
[[nodiscard]] bool append_banner_notification(Scratch& scratch,
                                              const queuez::SessionState& before,
                                              std::uint64_t familyRootSoid,
                                              std::span<const std::byte, state::kAesKeySize> key,
                                              std::array<std::byte, state::kBapNonceSize>& nonce,
                                              std::span<std::byte> response,
                                              std::size_t& written,
                                              queuez::SessionState& after) noexcept;

// the client holds the objIdx-1 buffer for one character at a time, so the pair moves with the pick
[[nodiscard]] bool
append_banner_move_notification(Scratch& scratch,
                                const queuez::SessionState& before,
                                std::uint64_t selectedCharacter,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written,
                                queuez::SessionState& after) noexcept;

[[nodiscard]] bool
append_change_character_notification(Scratch& scratch,
                                     const queuez::ChangeCharacter& change,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::span<const std::byte, state::kBapNonceSize> nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written) noexcept;

[[nodiscard]] bool
append_select_character_notification(Scratch& scratch,
                                     const queuez::SelectCharacter& select,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::span<const std::byte, state::kBapNonceSize> nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written) noexcept;

[[nodiscard]] bool
append_equipment_swap_notification(Scratch& scratch,
                                   const queuez::EquipmentSwap& swap,
                                   const state::PendingEquipmentSwap& mutation,
                                   std::span<const std::byte, state::kAesKeySize> key,
                                   std::span<const std::byte, state::kBapNonceSize> nonce,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool
append_item_state_notification(Scratch& scratch,
                               const queuez::EquipmentSwap& update,
                               const state::PendingItemState& mutation,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::span<const std::byte, state::kBapNonceSize> nonce,
                               std::span<std::byte> response,
                               std::size_t& written) noexcept;

[[nodiscard]] bool
append_equipment_appearance_refresh_notification(Scratch& scratch,
                                                 const queuez::CharacterAppearanceRefresh& refresh,
                                                 const state::PendingEquipmentSwap& mutation,
                                                 std::span<const std::byte, state::kAesKeySize> key,
                                                 std::array<std::byte, state::kBapNonceSize>& nonce,
                                                 std::span<std::byte> response,
                                                 std::size_t& written) noexcept;

[[nodiscard]] bool
append_socket_appearance_refresh_notification(Scratch& scratch,
                                              const queuez::CharacterAppearanceRefresh& refresh,
                                              const state::PendingSocketPlug& mutation,
                                              std::span<const std::byte, state::kAesKeySize> key,
                                              std::array<std::byte, state::kBapNonceSize>& nonce,
                                              std::span<std::byte> response,
                                              std::size_t& written) noexcept;

[[nodiscard]] bool
append_equipment_roster_refresh_notification(Scratch& scratch,
                                             const queuez::RosterAppearanceRefresh& refresh,
                                             const state::PendingEquipmentSwap& mutation,
                                             std::span<const std::byte, state::kAesKeySize> key,
                                             std::array<std::byte, state::kBapNonceSize>& nonce,
                                             std::span<std::byte> response,
                                             std::size_t& written) noexcept;

[[nodiscard]] bool
append_socket_roster_refresh_notification(Scratch& scratch,
                                          const queuez::RosterAppearanceRefresh& refresh,
                                          const state::PendingSocketPlug& mutation,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::array<std::byte, state::kBapNonceSize>& nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written) noexcept;

[[nodiscard]] bool
append_account_resync_appearance_notification(Scratch& scratch,
                                              const queuez::SessionState& before,
                                              std::span<const std::byte, state::kAesKeySize> key,
                                              std::array<std::byte, state::kBapNonceSize>& nonce,
                                              std::span<std::byte> response,
                                              std::size_t& written,
                                              queuez::SessionState& after) noexcept;

[[nodiscard]] bool
append_account_resync_roster_notification(Scratch& scratch,
                                          const queuez::SessionState& before,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::array<std::byte, state::kBapNonceSize>& nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written,
                                          queuez::SessionState& after) noexcept;

[[nodiscard]] bool
append_socket_plug_notification(Scratch& scratch,
                                const queuez::SocketPlug& socketPlug,
                                const state::PendingSocketPlug& mutation,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::span<const std::byte, state::kBapNonceSize> nonce,
                                std::span<std::byte> response,
                                std::size_t& written) noexcept;

[[nodiscard]] bool
append_item_acquisition_notification(Scratch& scratch,
                                     const queuez::ItemAcquisition& acquisition,
                                     const state::PendingItemAcquisition& mutation,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::span<const std::byte, state::kBapNonceSize> nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written) noexcept;

[[nodiscard]] bool
append_profile_item_acquisition_notification(Scratch& scratch,
                                             const queuez::ProfileItemAcquisition& acquisition,
                                             const state::PendingProfileItemAcquisition& mutation,
                                             std::span<const std::byte, state::kAesKeySize> key,
                                             std::span<const std::byte, state::kBapNonceSize> nonce,
                                             std::span<std::byte> response,
                                             std::size_t& written) noexcept;

[[nodiscard]] bool
append_item_dismantle_notification(Scratch& scratch,
                                   const queuez::ItemDismantle& dismantle,
                                   const state::PendingItemDismantle& mutation,
                                   std::span<const std::byte, state::kAesKeySize> key,
                                   std::span<const std::byte, state::kBapNonceSize> nonce,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool
append_vault_transfer_notification(Scratch& scratch,
                                   const queuez::VaultTransfer& transfer,
                                   const state::PendingVaultTransfer& mutation,
                                   std::span<const std::byte, state::kAesKeySize> key,
                                   std::span<const std::byte, state::kBapNonceSize> nonce,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool
append_ability_selection_notification(Scratch& scratch,
                                      const queuez::EquipmentSwap& update,
                                      const state::PendingAbilitySelection& mutation,
                                      std::span<const std::byte, state::kAesKeySize> key,
                                      std::span<const std::byte, state::kBapNonceSize> nonce,
                                      std::span<std::byte> response,
                                      std::size_t& written) noexcept;

[[nodiscard]] bool
append_ability_appearance_refresh_notification(Scratch& scratch,
                                               const queuez::CharacterAppearanceRefresh& refresh,
                                               const state::PendingAbilitySelection& mutation,
                                               std::span<const std::byte, state::kAesKeySize> key,
                                               std::array<std::byte, state::kBapNonceSize>& nonce,
                                               std::span<std::byte> response,
                                               std::size_t& written) noexcept;

[[nodiscard]] bool
append_ability_roster_refresh_notification(Scratch& scratch,
                                           const queuez::RosterAppearanceRefresh& refresh,
                                           const state::PendingAbilitySelection& mutation,
                                           std::span<const std::byte, state::kAesKeySize> key,
                                           std::array<std::byte, state::kBapNonceSize>& nonce,
                                           std::span<std::byte> response,
                                           std::size_t& written) noexcept;

} // namespace push

} // namespace dawn::server::bap::encrypted
