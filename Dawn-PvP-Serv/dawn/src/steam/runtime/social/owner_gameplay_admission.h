#pragma once

#include <cstddef>
#include <cstdint>

#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/social/realtime_codec.h"

namespace dawn::steam::runtime::social::owner_gameplay_admission {

namespace wire = middleware::social::realtime;

enum class Rejection : std::uint8_t {
    none,
    wrongKind,
    staleSequence,
    wrongLobby,
    localNotOwner,
    wrongRecipient,
    invalidSource,
    memberMissing,
    wrongPayloadSize,
    invalidMachine,
    invalidAddress,
    invalidPort,
    invalidMethod,
    wrongSession,
};

struct Decision final {
    std::size_t descriptorSize{};
    Rejection rejection{Rejection::wrongKind};

    [[nodiscard]] bool accepted() const noexcept {
        return rejection == Rejection::none;
    }
};

[[nodiscard]] inline const char* rejection_name(Rejection rejection) noexcept {
    switch (rejection) {
    case Rejection::none: return "none";
    case Rejection::wrongKind: return "wrong_kind";
    case Rejection::staleSequence: return "stale_sequence";
    case Rejection::wrongLobby: return "wrong_lobby";
    case Rejection::localNotOwner: return "local_not_owner";
    case Rejection::wrongRecipient: return "wrong_recipient";
    case Rejection::invalidSource: return "invalid_source";
    case Rejection::memberMissing: return "member_missing";
    case Rejection::wrongPayloadSize: return "wrong_payload_size";
    case Rejection::invalidMachine: return "invalid_machine";
    case Rejection::invalidAddress: return "invalid_address";
    case Rejection::invalidPort: return "invalid_port";
    case Rejection::invalidMethod: return "invalid_method";
    case Rejection::wrongSession: return "wrong_session";
    }
    return "unknown";
}

[[nodiscard]] inline bool contains_member(const wire::Lobby& lobby,
                                          std::uint64_t platformId) noexcept {
    if (platformId == 0 || lobby.memberCount > lobby.members.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        if (lobby.members[index].platformId == platformId) {
            return true;
        }
    }
    return false;
}

namespace detail {

[[nodiscard]] constexpr std::uint64_t read_little(const std::byte* bytes,
                                                  std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint64_t read_network(const std::byte* bytes,
                                                   std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value = (value << 8U) | std::to_integer<std::uint64_t>(bytes[index]);
    }
    return value;
}

} // namespace detail

// unlike callback 337 this carries the server's native 128-byte descriptor. Kept private
// so Destiny does not treat the present lobby owner as a second public joiner
[[nodiscard]] inline Decision validate(const wire::Lobby& lobby,
                                       std::uint64_t localPlatformId,
                                       const wire::Event& event,
                                       std::uint64_t lastAcceptedSequence) noexcept {
    if (event.kind != wire::EventKind::ownerGameplayAdmission) {
        return {0, Rejection::wrongKind};
    }
    if (event.sequence == 0 || event.sequence <= lastAcceptedSequence) {
        return {0, Rejection::staleSequence};
    }
    if (lobby.id == 0 || event.lobbyId != lobby.id) {
        return {0, Rejection::wrongLobby};
    }
    if (localPlatformId == 0 || lobby.ownerPlatformId != localPlatformId
        || !contains_member(lobby, localPlatformId)) {
        return {0, Rejection::localNotOwner};
    }
    if (event.memberPlatformId != localPlatformId) {
        return {0, Rejection::wrongRecipient};
    }
    if (event.sourcePlatformId == 0 || event.sourcePlatformId == localPlatformId) {
        return {0, Rejection::invalidSource};
    }
    if (!contains_member(lobby, event.sourcePlatformId)) {
        return {0, Rejection::memberMissing};
    }
    if (event.payloadSize != middleware::gameplay::descriptor::kDescriptorSize) {
        return {0, Rejection::wrongPayloadSize};
    }

    const std::byte* const descriptor = event.payload.data();
    if (detail::read_little(descriptor, sizeof(std::uint64_t)) == 0) {
        return {0, Rejection::invalidMachine};
    }
    constexpr std::size_t kNetAddrOffset = 8;
    constexpr std::size_t kPublicAddressOffset = kNetAddrOffset + 30;
    constexpr std::size_t kPublicPortOffset = kNetAddrOffset + 34;
    const std::uint64_t address = detail::read_network(descriptor + kNetAddrOffset, 4);
    const std::uint64_t publicAddress =
        detail::read_network(descriptor + kPublicAddressOffset, 4);
    if (address == 0 || publicAddress != address) {
        return {0, Rejection::invalidAddress};
    }
    const std::uint64_t port = detail::read_little(descriptor + kNetAddrOffset + 4, 2);
    const std::uint64_t publicPort = detail::read_little(descriptor + kPublicPortOffset, 2);
    if (port == 0 || (port & 1U) != 0 || publicPort != port) {
        return {0, Rejection::invalidPort};
    }
    constexpr std::size_t kMethodOffset = 93;
    if (descriptor[kMethodOffset] != std::byte{}) {
        return {0, Rejection::invalidMethod};
    }
    constexpr std::size_t kSessionOffset = 110;
    if (detail::read_little(descriptor + kSessionOffset, sizeof(std::uint64_t)) != lobby.id) {
        return {0, Rejection::wrongSession};
    }
    return {middleware::gameplay::descriptor::kDescriptorSize, Rejection::none};
}

} // namespace dawn::steam::runtime::social::owner_gameplay_admission
