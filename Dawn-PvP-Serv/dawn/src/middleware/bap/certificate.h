#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::bap::certificate {

[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool encode_signed_response(std::span<const std::byte> signedCertificate,
                                          std::span<std::byte> output,
                                          std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::certificate
