#pragma once

#include <Windows.h>

#include <array>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../definition.h"

namespace dawn::state::content_manifest::fingerprint {

inline constexpr std::size_t kHashObjectCapacity = 512;

class Hasher final {
public:
    Hasher() noexcept;
    ~Hasher() noexcept;

    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;

    [[nodiscard]] bool ready() const noexcept;

    [[nodiscard]] bool update(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] bool finish(Fingerprint& output) noexcept;

private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_HASH_HANDLE hash_{};
    std::array<std::byte, kHashObjectCapacity> object_{};
};

[[nodiscard]] bool update_u16(Hasher& hasher, std::uint16_t value) noexcept;
[[nodiscard]] bool update_u32(Hasher& hasher, std::uint32_t value) noexcept;
[[nodiscard]] bool update_u64(Hasher& hasher, std::uint64_t value) noexcept;

[[nodiscard]] bool rows(std::span<const Row> rows, Fingerprint& output) noexcept;

[[nodiscard]] bool catalog(std::span<const Row> rows,
                           const Fingerprint& directoryFingerprint,
                           Fingerprint& output) noexcept;

void guid(const Fingerprint& buildFingerprint, Guid& output) noexcept;

} // namespace dawn::state::content_manifest::fingerprint
