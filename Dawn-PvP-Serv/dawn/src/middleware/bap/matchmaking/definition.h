#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::matchmaking {

inline constexpr std::size_t kJoinDescriptorSize = 128;
inline constexpr std::size_t kMaximumResponseBodySize = 148;

enum class RequestKind : std::uint8_t {
    none = 0,
    sessionSearch = 1,
    advertisementUpdate = 2,
    advertisementDelete = 3,
    configuration = 4,
    rejoinAdvertisementUpdate = 5,
    rejoinAdvertisementDelete = 6,
    locateSession = 7,
    liveStats = 8,
};

struct AdvertisementUpdate final {
    bool hasExistingId{};
    // client-supplied advertisement id; zero asks the server to assign one
    std::uint64_t existingId{};
    std::uint64_t variantKey{};
    bool hasDescriptor{};
    std::span<const std::byte> descriptor{};
};

struct Request final {
    RequestKind kind{RequestKind::none};
    bool hasRequestNonce{};
    std::uint64_t requestNonce{};
    AdvertisementUpdate advertisement{};
};

struct Response final {
    RequestKind kind{RequestKind::none};
    std::uint64_t advertisementId{};
    std::span<const std::byte> descriptor{};
    bool towerConfiguration{};
};

} // namespace dawn::middleware::bap::matchmaking
