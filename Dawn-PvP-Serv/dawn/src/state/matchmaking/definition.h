#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::state::matchmaking {

// two players hold primary/private channels at once before either opens PUBLIC TARGET
#if defined(RESTORATION_DEDICATED_SERVER)
inline constexpr std::size_t kContextCapacity = 32;
#else
inline constexpr std::size_t kContextCapacity = 4;
#endif
// headroom over the current valid variants, not a protocol limit; keeps eviction fixed
inline constexpr std::size_t kVariantCapacity = 16;
inline constexpr std::size_t kDescriptorSize = 128;
inline constexpr std::uint8_t kInvalidVariantSlot = static_cast<std::uint8_t>(kVariantCapacity);
inline constexpr std::uint64_t kAbsentAdvertisementId = 0;
inline constexpr std::uint64_t kFirstAdvertisementId = 1;
inline constexpr std::uint32_t kInvalidGeneration = 0;
inline constexpr std::uint64_t kInvalidRevision = 0;
inline constexpr std::uint64_t kInitialContextRevision = 1;
inline constexpr std::uint64_t kInitialAllocatorRevision = 1;

struct ContextHandle {
    std::size_t slot{};
    std::uint32_t generation{};
};

enum class MutationKind : std::uint8_t {
    none,
    updateVariant,
    createStandaloneLatest,
    socialSearch,
};

struct SocialSearchGrant {
    std::uint64_t account,target,session,generation,expires;
    std::size_t targetSlot;
    std::uint32_t targetGeneration;
    std::uint64_t targetRevision;
    std::array<std::uint64_t,32> partyAccounts;
    std::array<std::byte,kDescriptorSize> descriptor;
};

struct PendingMutation {
    MutationKind kind{};
    ContextHandle context{};
    std::uint64_t expectedContextRevision{};
    std::uint64_t expectedAllocatorRevision{};
    std::uint64_t expectedNextAdvertisementId{};
    std::uint64_t requestId{};
    std::uint64_t advertisementId{};
    std::uint64_t variant{};
    std::uint8_t targetSlot{kInvalidVariantSlot};
    bool consumesAllocator{};
    bool targetsExistingVariant{};
    bool hasDescriptor{};
    const std::byte* descriptorData{};
    std::size_t descriptorSize{};
    // authenticated caller and primary-search metadata, never taken from a peer SOID field
    std::uint64_t socialAccount{},socialPlatform{};
    std::uint32_t socialActivity{};
    bool socialPrimary{};
    std::array<std::uint64_t,32> socialParty{};
    SocialSearchGrant socialGrant{};
};

// callers must securely erase this after encoding
struct LatestSnapshot {
    std::uint64_t advertisementId{};
    bool hasDescriptor{};
    std::array<std::byte, kDescriptorSize> descriptor{};
};

struct VariantRecord {
    std::uint64_t advertisementId;
    std::uint64_t variant;
    std::uint64_t lastUpdatedRevision;
    bool occupied;
    bool hasDescriptor;
    std::array<std::byte, kDescriptorSize> descriptor;
};

struct ContextData {
    std::array<VariantRecord, kVariantCapacity> variants;
    std::uint64_t revision;
    std::uint64_t standaloneLatestId;
    std::uint8_t latestSlot;
    std::uint64_t socialAccount,socialPlatform;
    std::uint32_t socialActivity;
    bool socialPrimary;
    std::array<std::uint64_t,32> socialParty;
    SocialSearchGrant socialGrant;
};

struct ContextSlot {
    ContextData data;
    std::uint32_t generation;
    bool active;
};

static_assert(std::is_trivial_v<ContextSlot> && std::is_standard_layout_v<ContextSlot>);

struct MatchmakingState {
    std::array<ContextSlot, kContextCapacity> contexts{};
    std::uint64_t nextAdvertisementId{kFirstAdvertisementId};
    std::uint64_t allocatorRevision{kInitialAllocatorRevision};
    bool allocatorExhausted{};
};

} // namespace dawn::state::matchmaking
