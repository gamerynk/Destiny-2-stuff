#pragma once

#include "../../../middleware/gameplay/group/session_messages.h"

namespace dawn::server::gameplay::group::public_player_profile {

// keep the public ActivityClient's account/character association empty but supply the
// PC platform identity; Codec-A fields 1 and 5 are independent, outer envelope required
[[nodiscard]] inline bool populate(
    middleware::gameplay::group::MembershipPlayer& player,
    std::uint64_t authenticatedPlatformId,
    bool nativeIdentityPresent,
    const std::array<std::byte, 36>& nativeIdentity) noexcept {
    namespace wire = middleware::gameplay::group;
    if (authenticatedPlatformId == 0) {
        return false;
    }
    if (nativeIdentityPresent) {
        std::uint64_t nativePlatformId = 0;
        for (std::size_t index = 0; index < sizeof(nativePlatformId); ++index) {
            nativePlatformId |= std::to_integer<std::uint64_t>(nativeIdentity[index])
                                << (index * 8U);
        }
        if (nativePlatformId != authenticatedPlatformId) {
            return false;
        }
    }
    wire::MembershipPlayer staged = player;
    staged.profilePresent = true;
    staged.profileType = 0;
    staged.emblemDefinitionIndex = 0;
    staged.light = 0;
    staged.displayNamePresent = false;
    staged.displayName = {};
    staged.profileSupplement = {};
    if (nativeIdentityPresent) {
        staged.identityPresent = true;
        staged.identity = nativeIdentity;
    } else if (!wire::set_synthesized_pc_platform_identity(staged, authenticatedPlatformId)) {
        return false;
    }
    player = staged;
    return true;
}

// roster collector (86657 RVA BE7850) feeds canonical-player +0x20 to BE4D00, which
// overwrites the contact-cache name, so an identity without a name would show empty
[[nodiscard]] inline bool populate_named(
    middleware::gameplay::group::MembershipPlayer& player,
    std::uint64_t authenticatedPlatformId,
    bool nativeIdentityPresent,
    const std::array<std::byte, 36>& nativeIdentity,
    std::string_view authenticatedDisplayName) noexcept {
    auto staged = player;
    if (!populate(staged, authenticatedPlatformId, nativeIdentityPresent, nativeIdentity)
        || !middleware::gameplay::group::set_ascii_membership_display_name(
            staged, authenticatedDisplayName)) {
        return false;
    }
    player = staged;
    return true;
}

// native killer-card extraction (86657 RVA 138D920) resolves the character cache through
// the account/character pair. Reject an incomplete presentation atomically
[[nodiscard]] inline bool populate_presented(
    middleware::gameplay::group::MembershipPlayer& player,
    std::uint64_t authenticatedPlatformId,
    bool nativeIdentityPresent,
    const std::array<std::byte, 36>& nativeIdentity,
    std::string_view authenticatedDisplayName,
    const middleware::gameplay::group::MembershipPlayer& presentation) noexcept {
    const auto& association = presentation.profileSupplement;
    if (!presentation.profilePresent || !association.field5Present
        || association.field5[0] == 0 || association.field5[1] == 0
        || !association.field6Present || presentation.light < 0) {
        return false;
    }
    auto staged = player;
    if (!populate_named(staged, authenticatedPlatformId, nativeIdentityPresent,
                        nativeIdentity, authenticatedDisplayName)) {
        return false;
    }
    staged.profileType = presentation.profileType;
    staged.emblemDefinitionIndex = presentation.emblemDefinitionIndex;
    staged.light = presentation.light;
    staged.profileSupplement = association;
    player = staged;
    return true;
}

} // namespace dawn::server::gameplay::group::public_player_profile
