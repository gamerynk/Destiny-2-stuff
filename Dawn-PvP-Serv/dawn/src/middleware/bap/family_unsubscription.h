#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::family_unsubscription {

struct Request {
    std::uint8_t familyType{};
    std::uint64_t familyRootSoid{};
};

[[nodiscard]] bool parse(std::span<const std::byte> input, Request& request) noexcept;

} // namespace dawn::middleware::bap::family_unsubscription
