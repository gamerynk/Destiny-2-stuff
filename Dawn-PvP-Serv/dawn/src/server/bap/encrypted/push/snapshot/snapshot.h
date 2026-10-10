#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "../../../../../middleware/datagen/family4/character/acquisition_feedback.h"

#include "../../../../../middleware/datagen/family4/loadout/definition.h"
#include "../../../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../../../middleware/queuez/queuez_update.h"
#include "../../../../../middleware/queuez/subscription.h"
#include "../../../../../state/account/account_state.h"
#include "../../../../../state/social/definition.h"
#include "../../internal.h"

namespace dawn::server::bap::encrypted::push::snapshot {

using JoinDescriptor = std::array<
    std::byte,
    middleware::gameplay::descriptor::kDescriptorSize>;

using JoinDescriptorProvider = bool (*)(std::uint64_t lobbyId,
                                        std::uint64_t ownerPlatformId,
                                        JoinDescriptor& output) noexcept;

void set_join_descriptor_provider(JoinDescriptorProvider provider) noexcept;

inline constexpr std::size_t kFamily4IdentityObjectCount = 2;
inline constexpr std::size_t kObjectCapacity =
    kFamily4IdentityObjectCount
    + state::kCharacterCapacity * middleware::datagen::family4::loadout::kItemCapacity
    + state::account::inventory::kProfileActionSourceCapacity
    + state::account::inventory::kVaultItemCapacity;

struct Prepared {
    std::array<middleware::queuez::Object, kObjectCapacity> objects{};
    middleware::queuez::Family family{};
    std::size_t rawClearSize{};
    std::size_t compressedClearSize{};

    // default copying would leave family.objects pointing into the source descriptor array
    Prepared() noexcept = default;
    Prepared(const Prepared&) = delete;
    Prepared& operator=(const Prepared&) = delete;
    Prepared(Prepared&&) = delete;
    Prepared& operator=(Prepared&&) = delete;
};

[[nodiscard]] bool preflight_required_families() noexcept;

[[nodiscard]] bool prepare_initial(Scratch& scratch,
                                   const middleware::queuez::Subscription& subscription,
                                   Prepared& prepared,
                                   const state::social::RemoteAccount* liveRemote = nullptr,
                                   std::uint64_t authenticatedSelfRoot = 0,
                                   std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions = {}) noexcept;

[[nodiscard]] bool prepare_family4_refresh(Scratch& scratch,
                                           std::uint64_t familyRootSoid,
                                           std::int32_t version,
                                           Prepared& prepared,
                                           std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions = {}) noexcept;

[[nodiscard]] bool prepare_banner(Scratch& scratch,
                                  std::uint64_t familyRootSoid,
                                  std::int32_t version,
                                  std::uint64_t previousCharacter,
                                  Prepared& prepared) noexcept;

[[nodiscard]] bool
prepare_character_appearance_refresh(Scratch& scratch,
                                     const queuez::CharacterAppearanceRefresh& refresh,
                                     const state::CharacterState& afterCharacter,
                                     std::size_t characterIndex,
                                     std::uint8_t nativeEquipmentSlot,
                                     bool replaceCharacterRecord,
                                     Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_roster_appearance_refresh(Scratch& scratch,
                                                     const queuez::RosterAppearanceRefresh& refresh,
                                                     const state::CharacterState& afterCharacter,
                                                     std::size_t characterIndex,
                                                     Prepared& prepared) noexcept;

[[nodiscard]] bool prepare_roster_list_refresh(Scratch& scratch,
                                               std::int32_t version,
                                               std::uint64_t rootSoid,
                                               Prepared& prepared) noexcept;

} // namespace dawn::server::bap::encrypted::push::snapshot
