#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../definition.h"

namespace dawn::state::content_manifest::cache {

inline constexpr std::array<char, 8> kCacheMagic{'R', 'E', 'S', 'T', 'M', 'A', 'N', '1'};
// bumped to bind the manifest id to package sizes and write times; older caches let
// the client reuse stale header bytes
inline constexpr std::uint32_t kCacheVersion = 4;

#pragma pack(push, 1)
struct Header final {
    std::array<char, kCacheMagic.size()> magic{};
    std::uint32_t version{};
    std::uint32_t headerSize{};
    std::uint32_t rowSize{};
    std::uint32_t rowCount{};
    std::uint32_t reserved{};
    Fingerprint directoryFingerprint{};
    Fingerprint buildFingerprint{};
};

struct DiskRow final {
    std::array<char, kPackageNameCapacity> name{};
    std::uint16_t nameLength{};
    std::uint16_t packageId{};
    std::uint64_t buildSignature{};
};
#pragma pack(pop)

static_assert(sizeof(Header)
              == kCacheMagic.size() + 5 * sizeof(std::uint32_t) + 2 * kFingerprintSize);
static_assert(sizeof(DiskRow)
              == kPackageNameCapacity + 2 * sizeof(std::uint16_t) + sizeof(std::uint64_t));

} // namespace dawn::state::content_manifest::cache
