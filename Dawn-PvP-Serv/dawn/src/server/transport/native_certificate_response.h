#pragma once
#include "native_certificate_cache.h"
#include "../../middleware/bap/certificate.h"

namespace dawn::server::transport::native_certificate_response {
[[nodiscard]] inline std::uint16_t encode(std::span<const std::byte> request,
    bool matched, const native_certificate_cache::Snapshot& certificate,
    std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (matched && certificate.size <= certificate.bytes.size()
        && middleware::bap::certificate::encode_signed_response(
            std::span(certificate.bytes).first(certificate.size), output, written)) return 200;
    written = 0;
    std::array<std::byte, 32> key{};
    return middleware::bap::native_certificate::request_key(request, key) ? 503 : 400;
}
}
