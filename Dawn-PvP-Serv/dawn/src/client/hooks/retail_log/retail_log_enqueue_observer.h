#pragma once

#include <Windows.h>

#include <cstdint>

#include "../../hooking/detour.h"

namespace dawn::client::hooks::retail_log {

extern SRWLOCK g_lock;
extern hooking::detour::Handle g_handle;

[[nodiscard]] void* enqueue_entry_point() noexcept;

void begin_multiplayer_capture() noexcept;

void assert_verbosity() noexcept;

void register_schema_marker(std::uint32_t marker) noexcept;

} // namespace dawn::client::hooks::retail_log
