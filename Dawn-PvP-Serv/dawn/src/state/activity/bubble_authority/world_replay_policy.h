#pragma once
#include <cstdint>
namespace dawn::state::activity::bubble_authority {
// public type38 is excluded on purpose: re-entry may still be waiting on placed objects
template<class Contract>
inline constexpr bool public_world_registered(const Contract& contract,bool rosterPublished,
                                              bool awaitingClientSync) noexcept {
    return contract.joined&&contract.patchEpoch&&contract.identity&&contract.authoritative
        &&contract.region&&contract.refresh&&rosterPublished&&!awaitingClientSync;
}
struct RegisteredWorldAcknowledgement {
    std::uint64_t canonical{},world{},account{},platform{};
    std::int32_t reportedRegion{-1},publishedRegion{-1};
    bool authenticated{},foreign{},publicRecord{},joined{},nativeRegistered{},ownerLease{};
};
inline constexpr bool registered_world_replay_admitted(const RegisteredWorldAcknowledgement& ack) noexcept {
    return ack.canonical&&ack.world&&ack.world!=ack.canonical&&ack.account&&ack.platform
        &&ack.authenticated&&ack.foreign&&ack.publicRecord&&ack.joined
        &&ack.nativeRegistered&&ack.ownerLease&&ack.reportedRegion>=0
        &&ack.reportedRegion==ack.publishedRegion&&ack.reportedRegion<512;
}
}
