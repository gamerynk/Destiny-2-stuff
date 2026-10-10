#pragma once

#include <Windows.h>

#include <cstdint>

#include "../records/domains.h"

namespace dawn::state::build_data::cache::writer {

[[nodiscard]] bool payload_checksum(records::Domains domains, std::uint64_t& checksum) noexcept;

[[nodiscard]] bool write_payload(HANDLE file, records::Domains domains) noexcept;

} // namespace dawn::state::build_data::cache::writer
