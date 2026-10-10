#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::inventory::buckets {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Descriptor> descriptors) noexcept;

[[nodiscard]] bool replace(std::span<const Descriptor> descriptors) noexcept;

[[nodiscard]] bool find(std::uint8_t bucketId, Descriptor& descriptor) noexcept;

[[nodiscard]] bool snapshot(std::span<Descriptor> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::inventory::buckets
