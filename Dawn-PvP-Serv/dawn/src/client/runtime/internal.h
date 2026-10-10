#pragma once

#include <Windows.h>

#include <cstdint>

namespace dawn::client::runtime {

enum class StageState : std::uint8_t {
    pending,
    active,
    failed,
};

extern SRWLOCK g_lock;
extern StageState g_mainStage;
extern StageState g_graphicsStage;
extern StageState g_platformStage;
extern HMODULE g_platformModule;

} // namespace dawn::client::runtime
