#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dawn::state::account::settings::bindings {

enum class Action : std::uint8_t {
    fire,
    toggleZoom,
    holdZoom,
    melee,
    grenade,
    super,
    reload,
    lightAttack,
    heavyAttack,
    block,
    switchWeapons,
    nextWeapon,
    previousWeapon,
    primaryWeapon,
    specialWeapon,
    heavyWeapon,
    moveForward,
    moveBackward,
    moveLeft,
    moveRight,
    jump,
    toggleCrouch,
    holdCrouch,
    toggleSprint,
    holdSprint,
    vehicleBoost,
    vehicleBrake,
    vehicleZoom,
    vehicleFirePrimary,
    vehicleFireSecondary,
    vehicleExit,
    interact,
    highlightPlayer,
    emoteOne,
    emoteTwo,
    emoteThree,
    emoteFour,
    airMove,
    classAbility,
    deathCameraZoomIn,
    deathCameraZoomOut,
    pushToTalk,
    uiGamepadButtonBack,
    uiOpenDirector,
    uiOpenDirectorStoreTab,
    uiOpenDirectorPursuitsTab,
    uiOpenDirectorMapTab,
    uiOpenDirectorDestinationsTab,
    uiOpenDirectorRosterTab,
    uiOpenDirectorSeasonsTab,
    uiOpenStartMenuAlternative,
    uiOpenStartMenuRecordsTab,
    uiOpenStartMenuCollectionsTab,
    uiOpenStartMenuClanTab,
    uiOpenStartMenuInventoryTab,
    uiOpenStartMenuSettingsTab,
    uiOpenExitDialogConfirm,
    uiAbortActivity,
    uiTextChatToggleState,
    screenshot,
    count,
};

inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::count);

struct Binding {
    bool operator==(const Binding&) const = default;
    std::optional<std::uint16_t> primary;
    std::optional<std::uint16_t> secondary;
};

struct KeyBindings {
    bool operator==(const KeyBindings&) const = default;
    bool seeded{};
    bool useLocalSource{};
    std::array<Binding, kActionCount> values;
    bool configured{};
};

} // namespace dawn::state::account::settings::bindings
