#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../../../../../state/account/settings/key_bindings.h"

namespace dawn::middleware::datagen::family4::account::preferences {

inline constexpr std::size_t kRecordSize = 77;
inline constexpr std::size_t kAudioSelectorPaddingSize = 1;
inline constexpr std::size_t kIdentityTogglePaddingSize = 2;
inline constexpr std::size_t kCalibrationPaddingSize = 1;
inline constexpr std::size_t kFieldOfViewPaddingSize = 2;
inline constexpr std::size_t kBindingArrayPaddingSize = 3;

#pragma pack(push, 1)

struct Record {
    // version gate over motion blur, film grain and chromatic aberration. below 1 the client reseeds
    // them from local cvars on every sign-in, so a stack that supplies them keeps the gate closed
    std::int32_t postProcessingSeedVersion{};
    std::int8_t buttonLayout{};
    std::int8_t movementMode{};
    std::int8_t controllerLookSensitivity{};
    std::int8_t doublePressDelay{};
    std::int32_t mouseLookSensitivity{};
    float adsSensitivityModifier{};
    std::int8_t subtitlesMode{};
    std::int8_t textSize{};
    std::int8_t textColor{};
    std::int8_t textBackgroundStyle{};
    std::int8_t textBackgroundOpacity{};
    std::int8_t reservedTextMode{};
    std::int8_t subtitleOptionsEntry{};
    std::int8_t voiceOutputMode{};
    std::int8_t teamVoiceChannel{};
    std::int8_t brightness{};
    std::int8_t helmetMode{};
    std::int8_t colorblindMode{};
    std::int8_t reticleColor{};
    std::int8_t reservedAudioMode{};
    std::array<std::byte, kAudioSelectorPaddingSize> audioSelectorPadding{};
    std::int8_t audioMigrationVersion{};
    std::int8_t soundEffectsVolume{};
    std::int8_t dialogueVolume{};
    std::int8_t musicVolume{};
    std::int8_t chatVolume{};
    std::uint8_t muteWhenUnfocused{};
    std::uint8_t controllerInvertVertical{};
    std::uint8_t controllerInvertHorizontal{};
    std::uint8_t mouseInvertVertical{};
    std::uint8_t mouseInvertHorizontal{};
    std::uint8_t controllerAutoLookCentering{};
    std::uint8_t preferGoodConnection{};
    std::uint8_t controllerVibration{};
    std::uint8_t unidentifiedToggle{};
    std::uint8_t mouseAimSmoothing{};
    std::uint8_t controllerSwapShoulders{};
    std::array<std::byte, kIdentityTogglePaddingSize> identityTogglePadding{};
    std::uint8_t showRealNames{};
    std::uint8_t displayHints{};
    std::uint8_t showFps{};
    std::int8_t reticleLocation{};
    std::uint8_t clanInviteNotifications{};
    std::uint8_t profanityFilter{};
    std::int8_t backgroundOpacity{};
    std::int8_t hudOpacity{};
    std::uint8_t voiceChatEnabled{};
    std::int8_t whisperChatMode{};
    std::int8_t teamChatJoinMode{};
    std::int8_t localChatJoinMode{};
    std::int8_t clanChatJoinMode{};
    std::int8_t hdrMode{};
    std::array<std::byte, kCalibrationPaddingSize> calibrationPadding{};
    float calibrationPrimary{};
    float calibrationAlpha{};
    std::int8_t textChatMode{};
    std::int8_t chatAutoHideMode{};
    std::uint8_t motionBlurMirror{};
    std::uint8_t filmGrainMirror{};
    std::uint8_t chromaticAberrationMirror{};
};

struct BindingsRecord {
    // 0 leaves local account and keybinding values open to the initial seed pass
    std::int32_t accountSeedVersion{};
    std::uint8_t voiceChatMirror{};
    std::uint8_t verticalSyncMirror{};
    std::array<std::byte, kFieldOfViewPaddingSize> fieldOfViewPadding{};
    std::int32_t fieldOfViewAdjustment{};
    // 0 picks this replicated array after the client's initial seed pass
    std::uint8_t sourceSelector{};
    std::array<std::byte, kBindingArrayPaddingSize> bindingArrayPadding{};
    std::array<std::uint32_t, state::account::settings::bindings::kActionCount> keyBindings{};
};

#pragma pack(pop)

static_assert(sizeof(Record) == kRecordSize);
static_assert(sizeof(BindingsRecord)
              == 4 * sizeof(std::uint32_t)
                     + state::account::settings::bindings::kActionCount * sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<Record>);
static_assert(std::is_trivially_copyable_v<BindingsRecord>);

} // namespace dawn::middleware::datagen::family4::account::preferences
