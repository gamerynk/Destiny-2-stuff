#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "reader.h"

namespace dawn::middleware::content::packages::reader::parallel {

inline constexpr std::size_t kMaxWorkers = 4;

// the worker index picks the caller's storage and no two readers get the same one, so a
// visitor touching only that needs no lock
using Visitor = void (*)(void* context,
                         std::size_t worker,
                         std::uint32_t tag,
                         std::span<const std::byte> blob) noexcept;

// a block read waits on the disk before decoding, so readers overlap that wait;
// leaves cores for the game
[[nodiscard]] std::size_t worker_count() noexcept;

[[nodiscard]] bool read_tags(const Source& source,
                             std::span<const std::uint32_t> tags,
                             Visitor visitor,
                             void* context) noexcept;

struct Held {
    std::uint32_t tag{};
    std::span<const std::byte> blob;
};

[[nodiscard]] bool read_kept(const Source& source,
                             std::span<const std::uint32_t> tags,
                             std::vector<Held>& kept) noexcept;

void release() noexcept;

} // namespace dawn::middleware::content::packages::reader::parallel
