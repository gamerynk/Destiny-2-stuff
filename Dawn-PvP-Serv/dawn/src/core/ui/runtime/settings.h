#pragma once

#include <Windows.h>

namespace dawn::core::ui::runtime {

struct Settings {
    bool enabled{true};
    UINT toggleVirtualKey{VK_INSERT};
};

} // namespace dawn::core::ui::runtime
