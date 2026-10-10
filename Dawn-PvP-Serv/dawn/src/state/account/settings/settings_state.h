#pragma once

#include <cstdint>

#include "key_bindings.h"

namespace dawn::state::account::settings {

inline constexpr std::int8_t kCompletedAudioMigrationVersion = 8;

struct Controls {
    bool operator==(const Controls&) const = default;
    std::int8_t buttonLayout{};
    std::int8_t movementMode{};
    std::int8_t controllerLookSensitivity{};
    bool controllerInvertVertical{};
    bool controllerAutoLookCentering{};
    bool controllerVibration{};
    bool controllerSwapShoulders{};
    bool controllerInvertHorizontal{};
    std::int32_t mouseLookSensitivity{};
    bool mouseInvertVertical{};
    bool mouseInvertHorizontal{};
    bool unidentifiedToggle{};
    bool mouseAimSmoothing{};
    float adsSensitivityModifier{};
    std::int8_t doublePressDelay{};
};

struct Audio {
    bool operator==(const Audio&) const = default;
    std::int8_t voiceOutputMode{};
    std::int8_t teamVoiceChannel{};
    std::int8_t reservedMode{};
    std::int8_t migrationVersion{};
    std::int8_t chatVolume{};
    bool muteWhenUnfocused{};
    std::int8_t soundEffectsVolume{};
    std::int8_t dialogueVolume{};
    std::int8_t musicVolume{};
};

struct Display {
    bool operator==(const Display&) const = default;
    // native fov_adjustment_offset, degrees 55..105, default 85
    std::int32_t fieldOfView{85};
    std::int8_t brightness{};
    bool showFps{};
    std::int8_t hdrMode{};
    float calibrationPrimary{};
    float calibrationAlpha{};
};

struct Interface {
    bool operator==(const Interface&) const = default;
    std::int8_t subtitlesMode{};
    std::int8_t colorblindMode{};
    std::int8_t helmetMode{};
    std::int8_t hudOpacity{};
    bool displayHints{};
    std::int8_t backgroundOpacity{};
    std::int8_t reticleLocation{};
    std::int8_t reticleColor{};
    std::int8_t textSize{};
    std::int8_t textColor{};
    std::int8_t textBackgroundStyle{};
    std::int8_t textBackgroundOpacity{};
    std::int8_t reservedTextMode{};
    std::int8_t subtitleOptionsEntry{};
};

struct Social {
    bool operator==(const Social&) const = default;
    bool preferGoodConnection{};
    std::int8_t textChatMode{};
    bool showRealNames{};
    bool clanInviteNotifications{};
    bool profanityFilter{};
    bool voiceChatEnabled{};
    std::int8_t whisperChatMode{};
    std::int8_t teamChatJoinMode{};
    std::int8_t localChatJoinMode{};
    std::int8_t clanChatJoinMode{};
    std::int8_t chatAutoHideMode{};
};

struct AccountSettings {
    bool operator==(const AccountSettings&) const = default;
    Controls controls;
    Audio audio;
    Display display;
    Interface interface;
    Social social;
    bindings::KeyBindings keyBindings;
    bool profileSetupCompleted{};
    bool configured{};
};

[[nodiscard]] bool valid(const AccountSettings& value) noexcept;

} // namespace dawn::state::account::settings
