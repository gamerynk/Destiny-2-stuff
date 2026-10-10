#include "social_roster_encoder.h"

#include <algorithm>
#include <cstring>

#include "../definitions.h"
#include "../../social/realtime_codec.h"

namespace dawn::middleware::datagen::social_roster {
namespace {

template <typename T>
void write(std::span<std::byte> destination, std::size_t offset, const T& value) noexcept {
    std::memcpy(destination.data() + offset, &value, sizeof value);
}

} // namespace

static_assert(kActivityDefinitionOffset + sizeof(std::int16_t) <= kSocialRosterMemberSize);
static_assert(kPowerScalarOffset + sizeof(std::int32_t) <= kSocialRosterMemberSize);
static_assert(kEmblemVariantOffset + sizeof(std::uint16_t) <= kSocialRosterMemberSize);
static_assert(kSelectedCharacterSoidOffset + sizeof(std::uint64_t)
              <= kSocialRosterDirectorySize);
static_assert(kJoinCapacityExternalOffset + sizeof(std::uint8_t) <= kSocialRosterMemberSize);
static_assert(kJoinCapacityMatchedOffset + sizeof(std::uint8_t) <= kSocialRosterMemberSize);
static_assert(kJoinCapacityOffset + sizeof(std::uint8_t) <= kSocialRosterMemberSize);
static_assert(kJoinPrivacyOffset + sizeof(std::uint8_t) <= kSocialRosterMemberSize);
static_assert(kJoinClosedReasonsOffset + sizeof(std::uint8_t) <= kSocialRosterMemberSize);
static_assert(kPresentationGroupKeyOffset + sizeof(std::int32_t) <= kSocialRosterMemberSize);
static_assert(social::realtime::kMemberCapacity <= INT8_MAX);

bool encode_directory(std::uint64_t accountSoid,
                      std::uint64_t selectedCharacterSoid,
                      std::span<std::byte> destination) noexcept {
    if (destination.size() != kSocialRosterDirectorySize || accountSoid == 0
        || selectedCharacterSoid == 0) {
        return false;
    }
    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, 0, accountSoid);
    write(destination, sizeof accountSoid, accountSoid);
    write(destination, kSelectedCharacterSoidOffset, selectedCharacterSoid);
    return true;
}

bool encode_member(const Member& member,
                   std::span<std::byte> destination) noexcept {
    if (destination.size() != kSocialRosterMemberSize || member.accountSoid == 0
        || member.activityIndex < 0 || member.activityIndex > 4094) {
        return false;
    }
    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, 0, member.accountSoid);
    write(destination, kActivityDefinitionOffset, member.activityIndex);
    write(destination, kPowerScalarOffset, member.power);
    write(destination, kEmblemDefinitionOffset, member.emblemDefinition);
    write(destination, kEmblemVariantOffset, kEmptyDefinitionIndex);
    // the joinability manager ignores the content-definition maximum the roster UI shows. It
    // picks +0x38 or +0x39 for a capacity check; any active view left at zero reports full
    const auto joinCapacity = static_cast<std::uint8_t>(social::realtime::kMemberCapacity);
    write(destination, kJoinCapacityExternalOffset, joinCapacity);
    write(destination, kJoinCapacityMatchedOffset, joinCapacity);
    write(destination, kJoinCapacityOffset, joinCapacity);
    // build 86657: 1414B8ED0 copies member +0x40 into contact +0x60. Both 0 and -1 split into
    // singleton rows but only -1 uses the per-contact comparator, so use the native sentinel
    write(destination, kPresentationGroupKeyOffset, kUnassignedPresentationGroupKey);
    return true;
}

} // namespace dawn::middleware::datagen::social_roster
