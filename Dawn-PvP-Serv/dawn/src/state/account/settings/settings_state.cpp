#include "settings_state.h"

#include <array>
#include <cstddef>

namespace dawn::state::account::settings {
namespace {

template <typename Value> struct Range {
    Value minimum;
    Value maximum;
};

constexpr std::array<std::int8_t, 7> kButtonLayouts{0, 1, 2, 3, 5, 6, 9};
constexpr Range<std::int8_t> kMovementModes{0, 3};
constexpr Range<std::int8_t> kControllerSensitivity{0, 9};
constexpr Range<std::int32_t> kMouseSensitivity{1, 100};
constexpr Range<float> kAdsSensitivity{0.5F, 1.5F};
constexpr Range<std::int8_t> kDoublePressDelay{0, 4};

constexpr Range<std::int8_t> kTwoChoiceSelector{0, 1};
constexpr Range<std::int8_t> kVoiceOutputMode{0, 2};
constexpr Range<std::int8_t> kChatVolume{0, 8};
constexpr Range<std::int8_t> kAudioVolume{0, 10};

constexpr Range<std::int8_t> kBrightness{0, 6};
constexpr float kCalibrationPrimary = 10000.0F;
constexpr float kCalibrationAlpha = 0.0F;

constexpr Range<std::int8_t> kSubtitlesMode{0, 2};
constexpr Range<std::int8_t> kColorblindMode{0, 3};
constexpr Range<std::int8_t> kHudOpacity{0, 3};
constexpr Range<std::int8_t> kBackgroundOpacity{0, 4};
constexpr Range<std::int8_t> kReticleColor{0, 6};
constexpr Range<std::int8_t> kTextSize{0, 4};
constexpr Range<std::int8_t> kTextPresentationMode{0, 3};
constexpr std::int8_t kUnidentifiedTextValue = 0;
constexpr Range<std::int8_t> kTextChatMode{0, 3};

template <typename Value> [[nodiscard]] bool within(Value value, Range<Value> range) noexcept {
    return value >= range.minimum && value <= range.maximum;
}

template <typename Value, std::size_t Count>
[[nodiscard]] bool contains(const std::array<Value, Count>& values, Value candidate) noexcept {
    for (const Value value : values) {
        if (value == candidate) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool valid_controls(const Controls& value) noexcept {
    return contains(kButtonLayouts, value.buttonLayout)
           && within(value.movementMode, kMovementModes)
           && within(value.controllerLookSensitivity, kControllerSensitivity)
           && within(value.mouseLookSensitivity, kMouseSensitivity)
           && within(value.adsSensitivityModifier, kAdsSensitivity)
           && within(value.doublePressDelay, kDoublePressDelay);
}

[[nodiscard]] bool valid_audio(const Audio& value) noexcept {
    return within(value.voiceOutputMode, kVoiceOutputMode)
           && within(value.teamVoiceChannel, kTwoChoiceSelector)
           && within(value.reservedMode, kTwoChoiceSelector)
           && value.migrationVersion == kCompletedAudioMigrationVersion
           && within(value.chatVolume, kChatVolume)
           && within(value.soundEffectsVolume, kAudioVolume)
           && within(value.dialogueVolume, kAudioVolume) && within(value.musicVolume, kAudioVolume);
}

[[nodiscard]] bool valid_display(const Display& value) noexcept {
    return value.fieldOfView >= 55 && value.fieldOfView <= 105
           && within(value.brightness, kBrightness) && within(value.hdrMode, kTwoChoiceSelector)
           && value.calibrationPrimary == kCalibrationPrimary
           && value.calibrationAlpha == kCalibrationAlpha;
}

[[nodiscard]] bool valid_interface(const Interface& value) noexcept {
    return within(value.subtitlesMode, kSubtitlesMode)
           && within(value.colorblindMode, kColorblindMode)
           && within(value.helmetMode, kTwoChoiceSelector) && within(value.hudOpacity, kHudOpacity)
           && within(value.backgroundOpacity, kBackgroundOpacity)
           && within(value.reticleLocation, kTwoChoiceSelector)
           && within(value.reticleColor, kReticleColor) && within(value.textSize, kTextSize)
           && within(value.textColor, kTextPresentationMode)
           && within(value.textBackgroundStyle, kTextPresentationMode)
           && within(value.textBackgroundOpacity, kBackgroundOpacity)
           && value.reservedTextMode == kUnidentifiedTextValue
           && value.subtitleOptionsEntry == kUnidentifiedTextValue;
}

[[nodiscard]] bool valid_social(const Social& value) noexcept {
    return within(value.textChatMode, kTextChatMode)
           && within(value.whisperChatMode, kTwoChoiceSelector)
           && within(value.teamChatJoinMode, kTwoChoiceSelector)
           && within(value.localChatJoinMode, kTwoChoiceSelector)
           && within(value.clanChatJoinMode, kTwoChoiceSelector)
           && within(value.chatAutoHideMode, kTwoChoiceSelector);
}

} // namespace

bool valid(const AccountSettings& value) noexcept {
    return value.configured && value.keyBindings.configured && valid_controls(value.controls)
           && valid_audio(value.audio) && valid_display(value.display)
           && valid_interface(value.interface) && valid_social(value.social);
}

} // namespace dawn::state::account::settings
