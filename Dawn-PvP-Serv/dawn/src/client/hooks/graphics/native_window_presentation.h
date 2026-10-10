#pragma once

#include <Windows.h>

namespace dawn::client::hooks::graphics::native_window {

[[nodiscard]] bool resolve() noexcept;
[[nodiscard]] bool selected() noexcept;
[[nodiscard]] bool fallback_requested() noexcept;
[[nodiscard]] bool service_bootstrap() noexcept;
[[nodiscard]] bool shutdown() noexcept;
[[nodiscard]] bool in_preparation() noexcept;

class FrameScope final {
public:
    explicit FrameScope(HWND window) noexcept;
    ~FrameScope() noexcept;
    FrameScope(const FrameScope&) = delete;
    FrameScope& operator=(const FrameScope&) = delete;
private:
    bool admitted_{};
    bool outermost_{};
};

} // namespace dawn::client::hooks::graphics::native_window
