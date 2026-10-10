#pragma once

#include <cstdint>
#include <span>

#include "../../../middleware/content/packages/reader/reader.h"

namespace dawn::client::content::vendors {

// definitions are read only for vendors asked for by hash: each is over 100 KiB and the banks
// hold nowhere near all 511. Leftover room is filled from the head of the index
[[nodiscard]] bool build(const middleware::content::packages::reader::Source& source,
                         middleware::content::packages::reader::Scratch& scratch,
                         std::span<const std::uint32_t> namedHashes) noexcept;

} // namespace dawn::client::content::vendors
