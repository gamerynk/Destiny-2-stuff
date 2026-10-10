#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "reader.h"

namespace dawn::middleware::content::packages::reader {

struct Header {
    std::uint16_t version{};
    std::uint16_t packageId{};
    std::uint16_t patchId{};
    std::uint32_t entryCount{};
    std::uint32_t blockCount{};
    std::uint64_t entryTable{};
    std::uint64_t blockTable{};
};

[[nodiscard]] bool parse_header(std::span<const std::byte, layout::kHeaderSize> bytes,
                                Header& header) noexcept;

[[nodiscard]] bool parse_leaf(std::wstring_view fileName,
                              std::uint16_t& packageId,
                              std::uint32_t& patchIndex) noexcept;

[[nodiscard]] bool find_latest(std::wstring_view directory,
                               std::uint16_t packageId,
                               Path& stem,
                               std::uint32_t& patchIndex) noexcept;

[[nodiscard]] bool build_path(const Path& stem, std::uint32_t patchIndex, Path& path) noexcept;

// uses files no reader owns and locks, so nothing on a parallel read path may call it
[[nodiscard]] bool
read_at(const Path& path, std::uint64_t offset, std::span<std::byte> output) noexcept;

// nothing here is shared, so several readers can run this at once without locking
[[nodiscard]] bool read_at(Scratch& scratch,
                           const Path& path,
                           std::uint64_t offset,
                           std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::content::packages::reader
