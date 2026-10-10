#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../descriptor/join_descriptor.h"
#include "player_profile.h"

namespace dawn::middleware::gameplay::group {

inline constexpr std::uint8_t kPeerPropertiesId = 31;
inline constexpr std::uint8_t kBootMachineId = 33;
inline constexpr std::uint8_t kPlayerAddId = 34;
inline constexpr std::uint8_t kPlayerRemoveId = 36;
inline constexpr std::uint32_t kPeerPropertiesSize = 408;
inline constexpr std::uint32_t kBootMachineSize = 100;
inline constexpr std::uint32_t kPlayerAddSize = 288;
inline constexpr std::uint32_t kPlayerRemoveSize = 12;

struct PeerPropertiesHeader {
    std::uint64_t sessionId{};
    std::uint8_t addressMethod{};
};

struct PlayerAddRequest {
    std::uint64_t sessionId{};
    std::uint64_t playerId{};
    std::uint32_t sequence{};
    std::uint8_t kind{};
    bool identityPresent{};
    std::array<std::byte, 36> identity{};
    PlayerProfileSupplement profileSupplement{};
};

struct BootMachineRequest {
    std::uint64_t sessionId{};
    std::uint8_t kind{};
    std::array<std::byte, descriptor::kNetAddrSize> address{};
};

struct PlayerRemoveRequest {
    std::uint64_t sessionId{};
    std::uint8_t playerIndex{};
};

[[nodiscard]] bool read_peer_properties_header(encoding::bits::Reader& reader,
                                               PeerPropertiesHeader& output) noexcept;

[[nodiscard]] bool read_boot_machine(encoding::bits::Reader& reader,
                                     BootMachineRequest& output) noexcept;

[[nodiscard]] bool read_player_add(encoding::bits::Reader& reader,
                                   PlayerAddRequest& output) noexcept;

[[nodiscard]] bool read_player_remove(encoding::bits::Reader& reader,
                                      PlayerRemoveRequest& output) noexcept;

} // namespace dawn::middleware::gameplay::group
