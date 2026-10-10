#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_host {

[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::uint32_t relayAddress,
                                   std::uint16_t relayPort,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::activity_host
