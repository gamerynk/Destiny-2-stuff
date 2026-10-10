#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../../middleware/datagen/family4/loadout/definition.h"
#include "../../../../state/account/account_state.h"
#include "../../remote_appearance_policy.h"

namespace dawn::server::bap::encrypted::queuez {

inline constexpr std::uint32_t kBannerFamilyType = 0;
inline constexpr std::uint32_t kSocialRosterFamilyType = 2;
inline constexpr std::uint32_t kRosterFamilyType = 3;
inline constexpr std::uint32_t kAccountFamilyType = 4;
inline constexpr std::int32_t kInitialFamilyVersion = 0;
// one id per character-owned item and per resident-backed mod/shader stack; currency rows carry
// no instance SOID. Matches the snapshot descriptor size so a snapshot that builds always stages
inline constexpr std::size_t kResidentCapacity =
    2 + state::kCharacterCapacity * middleware::datagen::family4::loadout::kItemCapacity
    + state::account::inventory::kProfileActionSourceCapacity
    + state::account::inventory::kVaultItemCapacity;

enum class Family3Phase : std::uint8_t {
    normal,
    publishOnce,
    responseOnly,
};

struct ResidentObject {
    std::uint64_t objectSoid{};
    std::uint32_t definitionId{};
};

struct SessionState {
    std::uint64_t family4RootSoid{};
    std::uint64_t family3RootSoid{};
    std::array<ResidentObject, kResidentCapacity> family4Residents{};
    std::uint64_t family0Character{};
    std::int32_t family4Version{};
    std::int32_t family3Version{};
    std::int32_t family0Version{};
    std::uint16_t family4ResidentCount{};
    Family3Phase family3Phase{Family3Phase::normal};
    bool family4Active{};
    bool family3Active{};
    bool family0Active{};
    // family-zero subscribe that arrived before any pick; the peer never asks again, so answer it at
    // the first pick. zero once sent
    std::uint64_t pendingBannerRoot{};
};

struct ChangeCharacter {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
};

// a changed character is deleted at its old key before the new key is upserted; picking the
// resident one is an in-place upsert with no release. A second resident in a full slot raises error 4
struct SelectCharacter {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t characterDefinitionId{};
    std::uint64_t previousCharacterSoid{};
    std::uint64_t selectedCharacterSoid{};
    // after opcode 505 a resent account body wipes the resident settings block, so only the
    // selected-character field goes out (the first pick replaces it whole)
    bool patchAccount{};
};

struct EquipmentSwap {
    SessionState after{};
    std::uint32_t characterDefinitionId{};
    std::uint64_t characterSoid{};
};

struct CharacterAppearanceRefresh {
    SessionState after{};
    std::uint64_t characterSoid{};
};

struct RosterAppearanceRefresh {
    SessionState after{};
    std::uint64_t characterSoid{};
    bool includeRoster{};
};

struct ItemAcquisition {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t characterDefinitionId{};
    std::uint32_t itemInstanceDefinitionId{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t acquiredInstanceSoid{};
    bool updatesAccount{};
};

struct SocketPlug {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t itemInstanceDefinitionId{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t targetInstanceSoid{};
    bool updatesAccount{};
};

struct ProfileItemAcquisition {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t itemInstanceDefinitionId{};
    std::uint64_t accountSoid{};
    std::uint64_t acquiredInstanceSoid{};
    bool actionSource{};
    bool appendedResident{};
};

struct VaultTransfer {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t characterDefinitionId{};
    std::uint32_t itemInstanceDefinitionId{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t instanceSoid{};
};

struct ItemDismantle {
    SessionState after{};
    std::uint32_t accountDefinitionId{};
    std::uint32_t characterDefinitionId{};
    std::uint32_t itemInstanceDefinitionId{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t dismantledInstanceSoid{};
    bool updatesAccount{};
};

struct StagedPublication {
    SessionState after{};
    remote_appearance::Published foreignAppearance{};
    std::uint64_t foreignAppearanceUnsubscribeRoot{};
    bool hasState{};
    bool armsFamily4Repush{};
    std::uint64_t family4RepushRoot{};
    // an answer inside the same exchange reaches the record before it writes new state and is
    // refused; the delayed copy is the one that lands
    bool armsBannerRepush{};
    std::uint64_t bannerRepushRoot{};
    std::uint64_t socialRosterRepushRoot{};
    std::uint64_t socialRosterUnsubscribeRoot{};
    bool rearmsSocialRosterRepush{};
    // the native owner descriptor is published async, so the client's one Family-7 subscribe can
    // arrive first; keep the root and replay it or the launch command sticks in running
    bool armsJoinRepush{};
    std::uint64_t joinRepushRoot{};
};

} // namespace dawn::server::bap::encrypted::queuez
