#pragma once

#include <cstddef>
#include <cstdint>

#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/social/realtime_codec.h"

namespace dawn::steam::runtime::social::central_gameplay_bootstrap {

namespace wire = middleware::social::realtime;

enum class Rejection : std::uint8_t {
    none,
    kind,
    sequence,
    lobby,
    owner,
    recipient,
    membership,
    payload,
    machine,
    address,
    port,
    method,
    session,
};

struct Decision final {
    Rejection rejection{Rejection::kind};

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return rejection == Rejection::none;
    }
};

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

[[nodiscard]] inline Decision validate(const wire::Lobby& lobby,
                                       std::uint64_t localPlatformId,
                                       const wire::Event& event) noexcept {
    if (event.kind != wire::EventKind::gameplayBootstrapDescriptor) {
        return {Rejection::kind};
    }
    if (event.sequence == 0) {
        return {Rejection::sequence};
    }
    if (lobby.id == 0 || event.lobbyId != lobby.id) {
        return {Rejection::lobby};
    }
    if (lobby.ownerPlatformId == 0 || event.sourcePlatformId != lobby.ownerPlatformId) {
        return {Rejection::owner};
    }
    if (localPlatformId == 0 || event.memberPlatformId != localPlatformId) {
        return {Rejection::recipient};
    }
    if (lobby.memberCount > lobby.members.size()) {
        return {Rejection::membership};
    }
    std::size_t localMatches = 0;
    std::size_t ownerMatches = 0;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        localMatches += lobby.members[index].platformId == localPlatformId ? 1U : 0U;
        ownerMatches += lobby.members[index].platformId == lobby.ownerPlatformId ? 1U : 0U;
    }
    if (localMatches != 1 || ownerMatches != 1) {
        return {Rejection::membership};
    }
    if (event.payloadSize != middleware::gameplay::descriptor::kDescriptorSize) {
        return {Rejection::payload};
    }

    const std::byte* const descriptor = event.payload.data();
    if (detail::read_little(descriptor, sizeof(std::uint64_t)) == 0) {
        return {Rejection::machine};
    }
    constexpr std::size_t kNetAddrOffset = 8;
    constexpr std::size_t kPublicAddressOffset = kNetAddrOffset + 30;
    const std::uint64_t address = detail::read_network(descriptor + kNetAddrOffset, 4);
    const std::uint64_t publicAddress =
        detail::read_network(descriptor + kPublicAddressOffset, 4);
    // over the internet the public address is the host's NAT address, never its local one,
    // so requiring a match rejected every remote host. Only no usable address is malformed
    if (address == 0 && publicAddress == 0) {
        return {Rejection::address};
    }
    const std::uint64_t port = detail::read_little(descriptor + kNetAddrOffset + 4, 2);
    // NAT remaps ports too, so the public port may differ; the local port keeps its rules
    if (port == 0 || (port & 1U) != 0) {
        return {Rejection::port};
    }
    constexpr std::size_t kMethodOffset = 93;
    if (descriptor[kMethodOffset] != std::byte{}) {
        return {Rejection::method};
    }
    constexpr std::size_t kSessionOffset = 110;
    if (detail::read_little(descriptor + kSessionOffset, sizeof(std::uint64_t)) != lobby.id) {
        return {Rejection::session};
    }
    return {Rejection::none};
}

[[nodiscard]] inline const char* rejection_name(Rejection rejection) noexcept {
    switch (rejection) {
    case Rejection::none:
        return "none";
    case Rejection::kind:
        return "kind";
    case Rejection::sequence:
        return "sequence";
    case Rejection::lobby:
        return "lobby";
    case Rejection::owner:
        return "owner";
    case Rejection::recipient:
        return "recipient";
    case Rejection::membership:
        return "membership";
    case Rejection::payload:
        return "payload";
    case Rejection::machine:
        return "machine";
    case Rejection::address:
        return "address";
    case Rejection::port:
        return "port";
    case Rejection::method:
        return "method";
    case Rejection::session:
        return "session";
    }
    return "unknown";
}

} // namespace dawn::steam::runtime::social::central_gameplay_bootstrap
