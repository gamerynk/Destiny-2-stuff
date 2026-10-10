#pragma once

#include <cstddef>
#include <cstdint>

#include "configured_item_detail_extractor.h"

namespace dawn::client::content::items::details::socket_reader {

namespace build_details = state::build_data::items::details;

[[nodiscard]] bool read_ordinary(const investment::Source& source,
                                 std::uintptr_t definition,
                                 std::size_t itemRowCount,
                                 build_details::Definition& output) noexcept;

[[nodiscard]] bool read_list_index(const investment::Source& source,
                                   std::uintptr_t definition,
                                   std::size_t rowCount,
                                   build_details::Definition& output) noexcept;

} // namespace dawn::client::content::items::details::socket_reader
