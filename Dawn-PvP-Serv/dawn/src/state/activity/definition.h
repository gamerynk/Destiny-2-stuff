#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "bubble_authority/definition.h"
#include "defaults/definition.h"
#include "destination/definition.h"
#include "entity_slots/definition.h"
#include "membership/definition.h"

namespace dawn::state::activity {

// must hold every live session at once: the private one plus one host per region.
// A full table evicts the oldest record, which can be the private one.
inline constexpr std::size_t kSessionCapacity = 16;
inline constexpr std::uint64_t kAbsentSessionId = 0;
inline constexpr std::uint64_t kClearedMemberKey = 0;
inline constexpr std::uint64_t kFirstSessionId = 1;
inline constexpr std::uint64_t kInvalidRevision = 0;
inline constexpr std::uint64_t kInitialStateRevision = 1;
inline constexpr std::uint64_t kInitialAllocatorRevision = 1;
inline constexpr std::size_t kInvalidSessionSlot = kSessionCapacity;
inline constexpr std::uint64_t kMaximumSessionId = (std::numeric_limits<std::uint64_t>::max)();
// revisions never wrap, or stale transactions could become valid again
inline constexpr std::uint64_t kMaximumRevision = (std::numeric_limits<std::uint64_t>::max)();

struct SessionRecord {
    destination::DestinationSelection destination{};
    entity_slots::LeaseMask heldEntitySlots{};
    // reserved for server-authored entities; disjoint from the held mask, never given to a client
    entity_slots::LeaseMask serverEntitySlots{};
    membership::MembershipState membership{};
    bubble_authority::AuthorityState bubbleAuthority{};
    std::uint64_t sessionId{};
    std::uint64_t ownerAccountSoid{};
    std::uint64_t memberKey{};
    std::uint64_t createdRevision{};
    std::uint64_t recordRevision{};
    std::uint64_t joinedRevision{};
    bool occupied{};
    bool joined{};
    // restored from a previous runtime and not joined yet; never serialized.
    // Joined/lease fields alone do not prove a live connection.
    bool restoredDormant{};
    std::uint64_t sharedOwnerAccountSoid{};
    std::uint64_t sharedOwnerCreatedRevision{};
    std::uint8_t sharedMemberSlot{};
    std::uint8_t sharedLeaseParts{};
    bool sharedOwnerDetached{};
    bool publicWorld{};
    bool socialWorld{};
};

struct PendingAllocation {
    destination::DestinationSelection destination{};
    membership::Reservations startupReservations{};
    membership::Reservations startupReservationsGuard{};
    membership::Identity startupOwner{};
    membership::Identity startupOwnerGuard{};
    std::uint64_t sessionId{};
    std::uint64_t soidBase{};
    std::uint64_t ownerAccountSoid{};
    std::uint64_t expectedStateRevision{};
    std::uint64_t expectedAllocatorRevision{};
    std::uint64_t expectedNextSessionId{};
    std::uint64_t selectedSessionCounter{};
    std::size_t targetSlot{kInvalidSessionSlot};
    bool prepared{};
    std::uint8_t publicLeaseParts{};
    std::uint8_t publicLeasePartsGuard{};
    bool socialWorld{};
    bool socialWorldGuard{};
    std::uint64_t socialSessionId{}, socialOwner{}, socialCreated{}, socialRevision{};
};

struct ActivityState {
    std::array<SessionRecord, kSessionCapacity> sessions{};
    defaults::ActivityDefaults defaults{};
    std::uint64_t stateRevision{kInitialStateRevision};
    std::uint64_t nextSessionId{kFirstSessionId};
    std::uint64_t allocatorRevision{kInitialAllocatorRevision};
    bool allocatorExhausted{};
};

} // namespace dawn::state::activity
