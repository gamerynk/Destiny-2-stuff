#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace dawn::middleware::bap::matchmaking::request {

struct Root final {
    RequestKind kind{RequestKind::none};
    bool hasRequestNonce{};
    std::uint64_t requestNonce{};
    bool hasAdvertisementMessage{};
    std::span<const std::byte> advertisementMessage{};
};

[[nodiscard]] bool parse_root(std::span<const std::byte> input, Root& selected) noexcept;

[[nodiscard]] bool parse_update(std::span<const std::byte> input,
                                AdvertisementUpdate& update) noexcept;

} // namespace dawn::middleware::bap::matchmaking::request
