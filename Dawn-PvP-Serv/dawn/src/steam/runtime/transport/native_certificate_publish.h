#pragma once
#include <cstdint>
#include <span>
#include <cstddef>
namespace dawn::steam::runtime::transport::native_certificate_publish {
void capture(std::span<const std::byte> callback, std::uint64_t platform) noexcept;
void service(std::uint64_t nowMilliseconds) noexcept;
// a server ack is not native CA verification or SetCertificate acceptance
[[nodiscard]] bool published() noexcept;
[[nodiscard]] bool durably_published() noexcept;
[[nodiscard]] bool durable_certificate_matches(std::span<const std::byte> nativePublicCertificate) noexcept;
void reset() noexcept;
}
