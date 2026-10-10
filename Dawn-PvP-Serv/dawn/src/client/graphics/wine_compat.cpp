#include "wine_compat.h"

#include <Windows.h>

namespace dawn::client::graphics {

void initialize_wine_display() noexcept {
    const HDC screen = GetDC(nullptr);
    if (screen != nullptr) {
        (void)ReleaseDC(nullptr, screen);
    }
}

} // namespace dawn::client::graphics
