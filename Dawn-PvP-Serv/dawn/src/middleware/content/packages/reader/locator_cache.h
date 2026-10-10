#pragma once

#include <cstdint>
#include <string_view>

#include "internal.h"

namespace dawn::middleware::content::packages::reader::locator_cache {

[[nodiscard]] std::uint64_t directory_hash(std::wstring_view value) noexcept;

[[nodiscard]] bool find(std::uint64_t directoryHash,
                        std::uint16_t packageId,
                        Path& stem,
                        std::uint32_t& patchIndex) noexcept;

void store(std::uint64_t directoryHash,
           std::uint16_t packageId,
           const Path& stem,
           std::uint32_t patchIndex) noexcept;

// a fully read directory answers a miss with no second pass and never writes again,
// so readers take the lookup lock in shared mode only
[[nodiscard]] bool complete(std::uint64_t directoryHash) noexcept;

void mark_complete(std::uint64_t directoryHash) noexcept;

} // namespace dawn::middleware::content::packages::reader::locator_cache
