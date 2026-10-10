#include "preferences_encoder.h"

#include "native_key_binding_map.h"

namespace dawn::middleware::datagen::family4::account::preferences {
namespace {

// unbound sentinel for native keybinding halves
constexpr std::uint16_t kUnboundInputCode = 0x0074;
// seed version 0 lets the client set up local mirrors once after sign-in
constexpr std::int32_t kOpenSeedVersion = 0;
// version 1 closes a gate so the client keeps the replicated values. the keybinding gate stays
// open for client defaults; post-processing stays closed or cvars overwrite its 3 fields each sign-in
constexpr std::int32_t kClosedSeedVersion = 1;
// source 0 makes later input reads use the replicated keybinding array
constexpr std::uint8_t kReplicatedBindingSource = 0;

[[nodiscard]] constexpr std::uint8_t native_boolean(bool value) noexcept {
    return value ? 1U : 0U;
}

[[nodiscard]] constexpr std::uint32_t
pack_binding(const state::account::settings::bindings::Binding& binding) noexcept {
    const std::uint32_t primary = binding.primary.value_or(kUnboundInputCode);
    const std::uint32_t secondary = binding.secondary.value_or(kUnboundInputCode);
    constexpr unsigned kSecondaryWordShift = 16;
    return (secondary << kSecondaryWordShift) | primary;
}

} // namespace

bool encode(const state::account::settings::AccountSettings& settings,
            Record& record,
            BindingsRecord& bindingsRecord) noexcept {
    if (!state::account::settings::valid(settings)) {
        return false;
    }

    record = {};
    bindingsRecord = {};
    record.postProcessingSeedVersion = kClosedSeedVersion;
    bindingsRecord.accountSeedVersion = settings.keyBindings.seeded ? kClosedSeedVersion : kOpenSeedVersion;
    bindingsRecord.sourceSelector = settings.keyBindings.useLocalSource ? 1 : kReplicatedBindingSource;

    const auto& controls = settings.controls;
    record.buttonLayout = controls.buttonLayout;
    record.movementMode = controls.movementMode;
    record.controllerLookSensitivity = controls.controllerLookSensitivity;
    record.doublePressDelay = controls.doublePressDelay;
    record.mouseLookSensitivity = controls.mouseLookSensitivity;
    record.adsSensitivityModifier = controls.adsSensitivityModifier;
    record.controllerInvertVertical = native_boolean(controls.controllerInvertVertical);
    record.controllerInvertHorizontal = native_boolean(controls.controllerInvertHorizontal);
    record.mouseInvertVertical = native_boolean(controls.mouseInvertVertical);
    record.mouseInvertHorizontal = native_boolean(controls.mouseInvertHorizontal);
    record.controllerAutoLookCentering = native_boolean(controls.controllerAutoLookCentering);
    record.controllerVibration = native_boolean(controls.controllerVibration);
    record.unidentifiedToggle = native_boolean(controls.unidentifiedToggle);
    record.mouseAimSmoothing = native_boolean(controls.mouseAimSmoothing);
    record.controllerSwapShoulders = native_boolean(controls.controllerSwapShoulders);

    const auto& audio = settings.audio;
    record.voiceOutputMode = audio.voiceOutputMode;
    record.teamVoiceChannel = audio.teamVoiceChannel;
    record.reservedAudioMode = audio.reservedMode;
    record.audioMigrationVersion = audio.migrationVersion;
    record.chatVolume = audio.chatVolume;
    record.muteWhenUnfocused = native_boolean(audio.muteWhenUnfocused);
    record.soundEffectsVolume = audio.soundEffectsVolume;
    record.dialogueVolume = audio.dialogueVolume;
    record.musicVolume = audio.musicVolume;

    const auto& display = settings.display;
    bindingsRecord.fieldOfViewAdjustment = display.fieldOfView;
    record.brightness = display.brightness;
    record.showFps = native_boolean(display.showFps);
    record.hdrMode = display.hdrMode;
    record.calibrationPrimary = display.calibrationPrimary;
    record.calibrationAlpha = display.calibrationAlpha;

    const auto& interfaceSettings = settings.interface;
    record.subtitlesMode = interfaceSettings.subtitlesMode;
    record.colorblindMode = interfaceSettings.colorblindMode;
    record.helmetMode = interfaceSettings.helmetMode;
    record.hudOpacity = interfaceSettings.hudOpacity;
    record.displayHints = native_boolean(interfaceSettings.displayHints);
    record.backgroundOpacity = interfaceSettings.backgroundOpacity;
    record.reticleLocation = interfaceSettings.reticleLocation;
    record.reticleColor = interfaceSettings.reticleColor;
    record.textSize = interfaceSettings.textSize;
    record.textColor = interfaceSettings.textColor;
    record.textBackgroundStyle = interfaceSettings.textBackgroundStyle;
    record.textBackgroundOpacity = interfaceSettings.textBackgroundOpacity;
    record.reservedTextMode = interfaceSettings.reservedTextMode;
    record.subtitleOptionsEntry = interfaceSettings.subtitleOptionsEntry;

    const auto& social = settings.social;
    record.preferGoodConnection = native_boolean(social.preferGoodConnection);
    record.textChatMode = social.textChatMode;
    record.showRealNames = native_boolean(social.showRealNames);
    record.clanInviteNotifications = native_boolean(social.clanInviteNotifications);
    record.profanityFilter = native_boolean(social.profanityFilter);
    record.voiceChatEnabled = native_boolean(social.voiceChatEnabled);
    record.whisperChatMode = social.whisperChatMode;
    record.teamChatJoinMode = social.teamChatJoinMode;
    record.localChatJoinMode = social.localChatJoinMode;
    record.clanChatJoinMode = social.clanChatJoinMode;
    record.chatAutoHideMode = social.chatAutoHideMode;
    bindingsRecord.voiceChatMirror = native_boolean(social.voiceChatEnabled);

    for (std::size_t nativeSlot = 0; nativeSlot < kActionsBySlot.size(); ++nativeSlot) {
        const auto action = kActionsBySlot[nativeSlot];
        const std::size_t stateIndex = static_cast<std::size_t>(action);
        // native ABI order is independent of the State enum order
        bindingsRecord.keyBindings[nativeSlot] =
            pack_binding(settings.keyBindings.values[stateIndex]);
    }
    return true;
}

} // namespace dawn::middleware::datagen::family4::account::preferences
