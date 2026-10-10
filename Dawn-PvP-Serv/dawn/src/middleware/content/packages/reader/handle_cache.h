#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "internal.h"

namespace dawn::middleware::content::packages::reader::handle_cache {

[[nodiscard]] bool
read(const Path& path, std::uint64_t offset, std::span<std::byte> output) noexcept;

[[nodiscard]] bool read(Scratch& scratch,
                        const Path& path,
                        std::uint64_t offset,
                        std::span<std::byte> output) noexcept;

void release() noexcept;

void close(Scratch& scratch) noexcept;

} // namespace dawn::middleware::content::packages::reader::handle_cache
