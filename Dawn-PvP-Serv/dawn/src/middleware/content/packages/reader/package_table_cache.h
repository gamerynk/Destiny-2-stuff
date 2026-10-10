#pragma once

#include <cstdint>

#include "internal.h"

namespace dawn::middleware::content::packages::reader::table_cache {

// the two tables are a few hundred KiB and a pass reads a package hundreds of thousands of
// times, so holding them removes the per-record read; they live in scratch, so no locking
[[nodiscard]] bool entry_record(Scratch& scratch,
                                const Path& path,
                                const Header& header,
                                std::uint32_t index,
                                layout::EntryRecord& record) noexcept;

[[nodiscard]] bool block_record(Scratch& scratch,
                                const Path& path,
                                const Header& header,
                                std::uint32_t index,
                                layout::BlockRecord& record) noexcept;

} // namespace dawn::middleware::content::packages::reader::table_cache
