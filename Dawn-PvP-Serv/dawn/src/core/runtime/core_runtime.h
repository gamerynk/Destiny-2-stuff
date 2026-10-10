#pragma once

namespace dawn::core {

[[nodiscard]] bool initialize(void* module) noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] bool is_initialized() noexcept;

} // namespace dawn::core
