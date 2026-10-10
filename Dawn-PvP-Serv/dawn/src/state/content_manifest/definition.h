#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::state::content_manifest {

// client ContentConfig array takes at most 4000 package rows
inline constexpr std::size_t kRowCapacity = 4000;
inline constexpr std::size_t kPackageNameCapacity = 256;
inline constexpr std::size_t kFingerprintSize = 32;
inline constexpr std::size_t kGuidCapacity = 37;
// package registrar only accepts public ids 0x0100 to 0x19FF
inline constexpr std::uint16_t kMinimumPackageId = 0x0100;
inline constexpr std::uint16_t kMaximumPackageId = 0x19FF;

using Fingerprint = std::array<std::byte, kFingerprintSize>;
using Guid = std::array<char, kGuidCapacity>;

struct Row final {
    std::array<char, kPackageNameCapacity> name{};
    std::uint16_t nameLength{};
    std::uint16_t packageId{};
    std::uint64_t buildSignature{};
};

struct View final {
    std::span<const Row> rows;
    std::string_view guid;
    std::span<const std::byte, kFingerprintSize> buildFingerprint;
};

using SnapshotVisitor = bool (*)(void* context, const View& view) noexcept;

} // namespace dawn::state::content_manifest
