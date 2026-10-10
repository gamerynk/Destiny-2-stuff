#pragma once
#include "../definition.h"
namespace dawn::state::activity::shared {
struct PublicMemberConnectionFence {
    std::uint64_t account{},source{},created{},joined{};
};
template<class Binding>
[[nodiscard]] constexpr bool public_connection_fence_matches(const Binding& binding,
    const PublicMemberConnectionFence& expected) noexcept {
    return expected.account && expected.source && expected.created && expected.joined
        && binding.account==expected.account && binding.source==expected.source
        && binding.sourceCreated==expected.created && binding.sourceJoined==expected.joined;
}
[[nodiscard]] inline bool retained_public_connection_allowed(const SessionRecord* current,
    const SessionRecord* participant,std::uint64_t hostCreated,std::uint8_t slot,bool sameIdentity) noexcept {
    return !(current && current->joined) && participant && participant->joined
        && !participant->restoredDormant && !participant->sharedOwnerDetached
        && participant->sharedOwnerCreatedRevision==hostCreated
        && participant->sharedMemberSlot==slot && participant->membership.hasIdentity && sameIdentity;
}
}
