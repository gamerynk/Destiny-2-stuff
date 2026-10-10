#pragma once

#include <cstdint>

#include "../../../../../core/filesystem/path.h"
#include "../../../definition.h"
#include "../../records/domains.h"

namespace dawn::state::build_data::cache::temporary {

[[nodiscard]] bool write(const wchar_t* finalPath,
                         const BuildIdentity& build,
                         records::Domains domains,
                         std::uint64_t checksum,
                         core::path::Buffer& temporaryPath) noexcept;

} // namespace dawn::state::build_data::cache::temporary
