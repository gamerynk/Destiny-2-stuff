#pragma once

#include <cstdint>

#include "../definition.h"
#include "records/domains.h"

namespace dawn::state::build_data::cache {

enum class LoadStatus {
    missing,
    loaded,
    stale,
    invalid,
};

enum class WriteDisposition {
    createOnly,
    replaceStale,
};

// isolated test hosts without a destiny2.exe sibling fall back to their own executable image
[[nodiscard]] bool current_build_identity(std::uint64_t configuredEquipmentHash,
                                          BuildIdentity& identity) noexcept;

[[nodiscard]] LoadStatus load(const wchar_t* path,
                              const BuildIdentity& expectedBuild,
                              records::MutableDomains output,
                              records::DomainCounts& counts) noexcept;

[[nodiscard]] bool write(const wchar_t* directory,
                         const wchar_t* path,
                         const BuildIdentity& build,
                         records::Domains domains,
                         WriteDisposition disposition) noexcept;

} // namespace dawn::state::build_data::cache
