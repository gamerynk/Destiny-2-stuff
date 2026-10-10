#pragma once

#define IMGUI_DISABLE_FILE_FUNCTIONS
// every Dear ImGui allocation must use Dawn's bounded static arena
#define IMGUI_DISABLE_DEFAULT_ALLOCATORS
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
// ImGui never polls the clipboard; the Logs page owns one explicit copy action
#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
