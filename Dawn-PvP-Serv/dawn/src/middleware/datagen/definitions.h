#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::middleware::datagen {

// object id the client matches against the family-three roster slot
inline constexpr std::uint32_t kRosterObjectId = 0x1C35451DU;
inline constexpr std::uint32_t kRosterCharacterObjectId = 0x63E0C862U;
inline constexpr std::uint32_t kBannerAnchorObjectId = 0xC6040EB0U;
inline constexpr std::uint32_t kBannerCharacterObjectId = 0xD859602CU;
inline constexpr std::uint32_t kAccountObjectId = 0x32D7B974U;
inline constexpr std::uint32_t kCharacterObjectId = 0xE5E86992U;
inline constexpr std::uint32_t kItemInstanceObjectId = 0x6CFBA3ABU;

// captured from all four occupied 1.59 Family-1 definitions
inline constexpr std::uint32_t kPublicProfileRootObjectId = 0xC47D63DBU;
inline constexpr std::uint32_t kPublicCharacterObjectId = 0x1A10412DU;
inline constexpr std::uint32_t kPublicAuxiliaryObjectId = 0x25D87B54U;
inline constexpr std::uint32_t kPublicItemInstanceObjectId = kItemInstanceObjectId;

// captured from the two occupied Family-6 native definition records
inline constexpr std::uint32_t kFireteamAnchorObjectId = 0xBF49D4F0U;
inline constexpr std::uint32_t kFireteamPartyObjectId = 0xE40CA32AU;

// captured from the two occupied 1.59 Family-7 join definition records
inline constexpr std::uint32_t kJoinAnchorObjectId = 0x05D07598U;
inline constexpr std::uint32_t kJoinDescriptorObjectId = 0x722C6528U;

inline constexpr std::uint32_t kSocialRosterDirectoryObjectId = 0xDA277CE4U;
inline constexpr std::uint32_t kSocialRosterMemberObjectId = 0x811115CEU;

inline constexpr std::size_t kSocialRosterDirectorySize = 96;
inline constexpr std::size_t kSocialRosterMemberSize = 80;

inline constexpr std::uint32_t kBannerFamily = 0;
inline constexpr std::uint32_t kPublicProfileFamily = 1;
inline constexpr std::uint32_t kSocialRosterFamily = 2;
inline constexpr std::uint32_t kRosterFamily = 3;
inline constexpr std::uint32_t kAccountFamily = 4;
inline constexpr std::uint32_t kFireteamFamily = 6;
inline constexpr std::uint32_t kJoinFamily = 7;

inline constexpr std::uint32_t kPublicProfileRootSlot = 0;
inline constexpr std::uint32_t kPublicCharacterSlot = 1;
inline constexpr std::uint32_t kPublicAuxiliarySlot = 2;
inline constexpr std::uint32_t kPublicItemInstanceSlot = 3;
inline constexpr std::uint32_t kRosterSlot = 0;
inline constexpr std::uint32_t kSocialRosterDirectorySlot = 0;
inline constexpr std::uint32_t kSocialRosterMemberSlot = 1;
inline constexpr std::uint32_t kAccountSlot = 0;
inline constexpr std::uint32_t kCharacterSlot = 1;
inline constexpr std::uint32_t kItemInstanceSlot = 3;
inline constexpr std::uint32_t kFireteamAnchorSlot = 0;
inline constexpr std::uint32_t kFireteamPartySlot = 1;
inline constexpr std::uint32_t kJoinAnchorSlot = 0;
inline constexpr std::uint32_t kJoinDescriptorSlot = 1;

[[nodiscard]] constexpr bool
object_id(std::uint32_t familyType, std::uint32_t slotIndex, std::uint32_t& objectId) noexcept {
    objectId = 0;
    if (familyType == kPublicProfileFamily && slotIndex == kPublicProfileRootSlot) {
        objectId = kPublicProfileRootObjectId;
    } else if (familyType == kPublicProfileFamily && slotIndex == kPublicCharacterSlot) {
        objectId = kPublicCharacterObjectId;
    } else if (familyType == kPublicProfileFamily && slotIndex == kPublicAuxiliarySlot) {
        objectId = kPublicAuxiliaryObjectId;
    } else if (familyType == kPublicProfileFamily && slotIndex == kPublicItemInstanceSlot) {
        objectId = kPublicItemInstanceObjectId;
    } else if (familyType == kSocialRosterFamily && slotIndex == kSocialRosterDirectorySlot) {
        objectId = kSocialRosterDirectoryObjectId;
    } else if (familyType == kSocialRosterFamily && slotIndex == kSocialRosterMemberSlot) {
        objectId = kSocialRosterMemberObjectId;
    } else if (familyType == kRosterFamily && slotIndex == kRosterSlot) {
        objectId = kRosterObjectId;
    } else if (familyType == kAccountFamily && slotIndex == kAccountSlot) {
        objectId = kAccountObjectId;
    } else if (familyType == kAccountFamily && slotIndex == kCharacterSlot) {
        objectId = kCharacterObjectId;
    } else if (familyType == kAccountFamily && slotIndex == kItemInstanceSlot) {
        objectId = kItemInstanceObjectId;
    } else if (familyType == kFireteamFamily && slotIndex == kFireteamAnchorSlot) {
        objectId = kFireteamAnchorObjectId;
    } else if (familyType == kFireteamFamily && slotIndex == kFireteamPartySlot) {
        objectId = kFireteamPartyObjectId;
    } else if (familyType == kJoinFamily && slotIndex == kJoinAnchorSlot) {
        objectId = kJoinAnchorObjectId;
    } else if (familyType == kJoinFamily && slotIndex == kJoinDescriptorSlot) {
        objectId = kJoinDescriptorObjectId;
    }
    return objectId != 0;
}

} // namespace dawn::middleware::datagen
