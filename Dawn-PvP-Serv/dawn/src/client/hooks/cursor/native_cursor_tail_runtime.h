#pragma once
#include <Windows.h>

namespace dawn::client::hooks::cursor::tail {
void bootstrap() noexcept;
[[nodiscard]] bool configured() noexcept;
[[nodiscard]] bool manages_policy() noexcept;
[[nodiscard]] bool omission_ready() noexcept;
[[nodiscard]] bool request_toggle(HWND window, UINT key) noexcept;
[[nodiscard]] bool prepare_shutdown() noexcept;
[[nodiscard]] bool prepare_retirement() noexcept;
void binding_started() noexcept;
void reset_after_uninstall() noexcept;

class OwnerScope final {
public:
    OwnerScope(HWND window, UINT message) noexcept;
    ~OwnerScope();
    OwnerScope(const OwnerScope&) = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;
private:
    HWND window_{};
    bool outer_{};
};
}
