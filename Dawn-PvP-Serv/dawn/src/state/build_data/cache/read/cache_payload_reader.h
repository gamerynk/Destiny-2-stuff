#pragma once

#include <Windows.h>

#include <cstdint>

#include "../records/domains.h"
#include "../records/format.h"

namespace dawn::state::build_data::cache::read {

template <typename Value> [[nodiscard]] bool read_value(HANDLE file, Value& value) noexcept {
    DWORD transferred = 0;
    return ReadFile(file, &value, static_cast<DWORD>(sizeof value), &transferred, nullptr) != FALSE
           && transferred == sizeof value;
}

void clear(records::MutableDomains output) noexcept;

[[nodiscard]] bool expected_size(const records::DomainCounts& counts, std::uint64_t& size) noexcept;

[[nodiscard]] bool read_payload(HANDLE file,
                                const records::InvestmentConstants& constants,
                                const records::DomainCounts& counts,
                                records::MutableDomains output,
                                std::uint64_t& checksum) noexcept;

} // namespace dawn::state::build_data::cache::read
