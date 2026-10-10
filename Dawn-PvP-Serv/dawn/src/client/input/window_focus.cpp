// focus is tested per process, not per window: the game has several windows and which one
// is in front changes with the display mode

#include "window_focus.h"

#include <Windows.h>

namespace dawn::client::input {

bool game_focused() noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD processId = 0;
    (void)GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

} // namespace dawn::client::input
