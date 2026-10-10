#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_reader.h"
#include "../../../core/protocol/activity_peer_connection.h"

namespace dawn::middleware::bap::activity_message::client_authoritative_data {

inline constexpr std::int8_t kMinimumState = -1;
inline constexpr std::int8_t kMaximumState = 6;
inline constexpr std::int32_t kAbsentSliceSetIndex = -1;
inline constexpr std::int32_t kMaximumSliceSetIndex = 1022;
inline constexpr std::int32_t kAbsentRegionIndex = -1;

struct SpawnState final {
    std::int8_t state{};
    std::uint8_t opaqueByte{};
    std::uint64_t opaqueValue{};
};

struct TeleportState final {
    std::int8_t state{};
    std::uint8_t token{};
    std::int32_t sliceSetIndex{kAbsentSliceSetIndex};
    std::uint32_t sliceSetHash{};
};

// the region the client says it is in; it moves as the player crosses a bubble boundary,
// so it beats the destination's slice set
struct RegionState final {
    std::int32_t index{kAbsentRegionIndex};
    std::uint32_t hash{};
    bool hasHash{};
};

struct ClientAuthoritativeData final {
    core::protocol::ActivityPeerConnection peerConnection{};
    SpawnState spawn{};
    TeleportState teleport{};
    RegionState region{};
    std::uint8_t transitionToken{};
    bool hasTransitionToken{};
    bool hasSpawn{};
    bool hasTeleport{};
    bool hasRegion{};
};

inline constexpr std::uint32_t kMessageType = 22;
inline constexpr std::size_t kMinimumEncodedSize = 1;
inline constexpr std::size_t kMaximumMeaningfulBitCount = 86'661;
inline constexpr std::size_t kMaximumEncodedSize = 10'833;

[[nodiscard]] bool parse_client_authoritative_data(std::span<const std::byte> input,
                                                   ClientAuthoritativeData& update) noexcept;

[[nodiscard]] bool read_presence(encoding::bits::Reader& reader, bool& present) noexcept;

[[nodiscard]] bool skip_opaque_root_branch(encoding::bits::Reader& reader) noexcept;

[[nodiscard]] bool read_peer_connection_branch(encoding::bits::Reader& reader,
    core::protocol::ActivityPeerConnection& output) noexcept;

[[nodiscard]] bool read_transition_branch(encoding::bits::Reader& reader,
                                          ClientAuthoritativeData& update) noexcept;

} // namespace dawn::middleware::bap::activity_message::client_authoritative_data
