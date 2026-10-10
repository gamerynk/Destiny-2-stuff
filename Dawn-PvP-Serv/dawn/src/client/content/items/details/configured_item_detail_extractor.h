#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../state/build_data/items/details/definition.h"
#include "../../investment/source.h"

namespace dawn::client::content::items::details {

[[nodiscard]] bool extract(const investment::Source& source,
                           std::span<const std::uint16_t> requestedDefinitionIndices,
                           std::size_t socketEntryListRowCount,
                           std::span<state::build_data::items::details::Definition> output,
                           std::size_t& count) noexcept;

} // namespace dawn::client::content::items::details
