#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::client::memory {

[[nodiscard]] bool
read_current_process(void* context, std::uintptr_t address, std::span<std::byte> output) noexcept;

} // namespace dawn::client::memory
