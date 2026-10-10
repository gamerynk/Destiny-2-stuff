#pragma once

#include <Windows.h>

namespace dawn::client::hooks::assert_handler {

extern SRWLOCK g_lock;
extern bool g_installed;

[[nodiscard]] void* handler_entry_point() noexcept;

} // namespace dawn::client::hooks::assert_handler
