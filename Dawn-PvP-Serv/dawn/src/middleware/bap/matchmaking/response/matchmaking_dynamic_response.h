#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::matchmaking::response {

[[nodiscard]] bool encode_advertisement_id(bool includeAssignmentResult,
                                           std::uint64_t advertisementId,
                                           std::span<std::byte> output,
                                           std::size_t& written) noexcept;

[[nodiscard]] bool encode_locate_result(std::uint64_t advertisementId,
                                        std::span<const std::byte> descriptor,
                                        std::span<std::byte> output,
                                        std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::matchmaking::response
