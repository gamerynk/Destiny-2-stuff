#pragma once

#include <cstddef>
#include <cstdint>

#include "configured_item_detail_extractor.h"

namespace dawn::client::content::items::details::reader {

namespace build_details = state::build_data::items::details;

struct Table {
    std::uintptr_t tableAddress{};
    std::uintptr_t rowsAddress{};
    std::size_t rowCount{};
};

[[nodiscard]] bool read_definition(const investment::Source& source,
                                   const Table& table,
                                   std::uint16_t definitionIndex,
                                   std::size_t socketEntryListRowCount,
                                   build_details::Definition& output) noexcept;

} // namespace dawn::client::content::items::details::reader
