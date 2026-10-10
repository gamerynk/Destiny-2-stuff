#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include "../../middleware/bap/native_certificate.h"

namespace dawn::server::transport::native_certificate_cache {
struct Snapshot final {
    std::array<std::byte, middleware::bap::native_certificate::kCapacity> bytes{};
    std::size_t size{};
};
// caller must authenticate the platform; shape/key/expiry are checked, native Steam verifies the CA signature
[[nodiscard]] bool publish(std::uint64_t platform, std::span<const std::byte> signedCertificate,
                           std::uint64_t nowSeconds, bool* durable = nullptr) noexcept;
[[nodiscard]] bool configure_storage(std::wstring_view directory) noexcept;
[[nodiscard]] bool initialize_storage() noexcept;
// only a live certificate for the authenticated platform AND the exact CSR key may be returned
[[nodiscard]] bool find(std::uint64_t platform, std::span<const std::byte> svc304,
                        std::uint64_t nowSeconds, Snapshot& output,
                        const char** reason = nullptr) noexcept;
void clear() noexcept;
}
