#pragma once

namespace dawn::client::hooks::bootflow {

void begin_native_fireteam_callback() noexcept;
void end_native_fireteam_callback() noexcept;
[[nodiscard]] bool native_fireteam_callback_is_outermost() noexcept;

void service_native_fireteam_callback() noexcept;
[[nodiscard]] bool native_camera_observers_available() noexcept;

class NativeFireteamCallbackScope final {
public:
    NativeFireteamCallbackScope() noexcept { begin_native_fireteam_callback(); }
    ~NativeFireteamCallbackScope() { end_native_fireteam_callback(); }
    NativeFireteamCallbackScope(const NativeFireteamCallbackScope&) = delete;
    NativeFireteamCallbackScope& operator=(const NativeFireteamCallbackScope&) = delete;
};

} // namespace dawn::client::hooks::bootflow
