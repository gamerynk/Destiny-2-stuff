#pragma once

#include <cstdint>
#include <span>

namespace dawn::client::content::handles {

using Reader = bool (*)(void* context,
                        std::uintptr_t address,
                        std::span<std::byte> output) noexcept;

struct Source {
    std::uintptr_t tablesSlot{};
    void* context{};
    Reader read{};
};

[[nodiscard]] bool
resolve(const Source& source, std::uint32_t handle, std::uintptr_t& address) noexcept;

// QueueZ schema descriptors share the datum layout but are not limited to table id 1024+
[[nodiscard]] bool
resolve_schema(const Source& source, std::uint32_t handle, std::uintptr_t& address) noexcept;

} // namespace dawn::client::content::handles
