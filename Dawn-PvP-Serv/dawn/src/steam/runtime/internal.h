#pragma once

namespace dawn::steam::runtime {

[[nodiscard]] bool activate_main_once() noexcept;

[[nodiscard]] bool main_activation_pending() noexcept;

void activate_graphics_once() noexcept;

void activate_platform_once(const void* callerAddress) noexcept;

} // namespace dawn::steam::runtime
