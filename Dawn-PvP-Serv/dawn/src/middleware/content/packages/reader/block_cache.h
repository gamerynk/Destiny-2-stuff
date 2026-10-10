#pragma once

#include <cstdint>
#include <span>

#include "internal.h"
#include "layout.h"
#include "reader.h"

namespace dawn::middleware::content::packages::reader::block_cache {

[[nodiscard]] bool
find(Scratch& scratch, std::uint64_t key, std::span<const std::byte>& plaintext) noexcept;

void store(Scratch& scratch,
           std::uint64_t key,
           std::span<const std::byte> decoded,
           std::span<const std::byte>& plaintext) noexcept;

[[nodiscard]] std::uint64_t key_of(std::uint16_t packageId,
                                   const layout::BlockRecord& record) noexcept;

[[nodiscard]] bool load_header(const Path& path,
                               std::uint16_t packageId,
                               std::uint32_t patchIndex,
                               Scratch& scratch,
                               Header& header) noexcept;

} // namespace dawn::middleware::content::packages::reader::block_cache
