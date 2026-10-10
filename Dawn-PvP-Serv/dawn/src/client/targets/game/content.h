#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../patterns/registry.h"

namespace dawn::client::targets::game::content {

struct Targets {
    std::byte* queuezObjectResolver{};
    std::byte* queuezFamily5Subscribe{};
    std::byte* queuezObjectStoreGetter{};
    std::byte* contentHandleTablesSlot{};
    std::byte* getItemStatValue{};
    std::byte* lightValueToScalar{};
    std::uint32_t queuezDescriptorFamilyBias{};
};

[[nodiscard]] bool resolve(std::span<const patterns::ImageRange> image) noexcept;

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

[[nodiscard]] bool report_queuez_descriptors_once() noexcept;

[[nodiscard]] bool report_family6_descriptors_once() noexcept;

void report_family6_lookup(std::uint32_t objectId,
                           std::int32_t result,
                           std::uintptr_t caller) noexcept;

[[nodiscard]] bool is_resolved() noexcept;

} // namespace dawn::client::targets::game::content
