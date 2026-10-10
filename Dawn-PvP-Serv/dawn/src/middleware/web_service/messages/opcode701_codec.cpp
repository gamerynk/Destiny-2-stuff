#include "opcode701.h"

#include <array>
#include <cstring>

#include "../../encoding/bit_reader.h"
#include "../../datagen/family4/account/preferences/preferences_encoder.h"
#include "../../datagen/family4/account/preferences/native_key_binding_map.h"

namespace dawn::middleware::web_service::messages::opcode701 {
namespace {
namespace preferences = datagen::family4::account::preferences;

struct Field {
    std::uint16_t offset;
    std::uint16_t count;
    std::uint16_t stride;
    std::uint8_t width;
    std::uint8_t bytes;
    bool optional;
    std::uint32_t bias;
    std::span<const Field> children;
};

#include "opcode701_schema.inl"

// update starts at native account + 0x748; setup confirmation precedes preferences
constexpr std::size_t kProfileSetupCompleted = 0x448;
constexpr std::size_t kPreferences = 0x44C;
constexpr std::size_t kBindings = 0x6F8;

struct Records {
    preferences::Record preferences{};
    preferences::BindingsRecord bindings{};
    bool touched{};
    bool voiceMirrorTouched{};
    bool fieldOfViewTouched{};
    bool profileSetupCompleted{};
};

void overlay(Records& records, std::size_t offset, std::uint8_t size,
             std::uint64_t value) noexcept {
    std::byte* target = nullptr;
    if (offset == kProfileSetupCompleted && size == 1) {
        records.profileSetupCompleted = value != 0;
        records.touched = true;
        return;
    }
    if (offset >= kPreferences && offset + size <= kPreferences + sizeof(records.preferences)) {
        target = reinterpret_cast<std::byte*>(&records.preferences) + offset - kPreferences;
    } else if (offset >= kBindings && offset + size <= kBindings + sizeof(records.bindings)) {
        target = reinterpret_cast<std::byte*>(&records.bindings) + offset - kBindings;
    }
    if (target != nullptr) {
        records.touched = true;
        if (offset == kBindings + offsetof(preferences::BindingsRecord, voiceChatMirror))
            records.voiceMirrorTouched = true;
        if (offset == kBindings + offsetof(preferences::BindingsRecord, fieldOfViewAdjustment))
            records.fieldOfViewTouched = true;
        for (std::uint8_t i = 0; i < size; ++i) {
            target[i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
        }
    }
}

bool read(encoding::bits::Reader& reader, std::span<const Field> fields,
          std::size_t base, Records& records) noexcept {
    for (const auto& field : fields) {
        for (std::size_t i = 0; i < field.count; ++i) {
            std::uint64_t value = 0;
            if (field.optional) {
                if (!reader.read(1, value)) return false;
                if (value == 0) continue;
            }
            const auto offset = base + field.offset + i * field.stride;
            if (!field.children.empty()) {
                if (!read(reader, field.children, offset, records)) return false;
            } else {
                if (!reader.read(field.width, value)) return false;
                overlay(records, offset, field.bytes, value - field.bias);
            }
        }
    }
    return true;
}

} // namespace

bool parse_settings(const Message& message,
    const state::account::settings::AccountSettings& before,
    state::account::settings::AccountSettings& output) noexcept {
    if (message.opcode != kOpcode) return false;
    Records records{};
    records.profileSetupCompleted = before.profileSetupCompleted;
    if (!preferences::encode(before, records.preferences, records.bindings)) return false;
    encoding::bits::Reader reader(message.payload);
    if (!read(reader, schema80807603, 0, records)) return false;
    const auto remaining = reader.remaining_bits();
    std::uint64_t trailer = 0;
    if (remaining < 2 || remaining > 9
        || !reader.read(static_cast<std::uint8_t>(remaining), trailer) || trailer != 0) return false;
    if (!records.touched) {
        output = before;
        return true;
    }
    auto candidate = before;
    candidate.profileSetupCompleted = records.profileSetupCompleted;
    const auto& record = records.preferences;
    const auto& bindings = records.bindings;
    candidate.controls.buttonLayout = record.buttonLayout;
    candidate.controls.movementMode = record.movementMode;
    candidate.controls.controllerLookSensitivity = record.controllerLookSensitivity;
    candidate.controls.doublePressDelay = record.doublePressDelay;
    candidate.controls.mouseLookSensitivity = record.mouseLookSensitivity;
    candidate.controls.adsSensitivityModifier = record.adsSensitivityModifier;
    candidate.controls.controllerInvertVertical = record.controllerInvertVertical != 0;
    candidate.controls.controllerInvertHorizontal = record.controllerInvertHorizontal != 0;
    candidate.controls.mouseInvertVertical = record.mouseInvertVertical != 0;
    candidate.controls.mouseInvertHorizontal = record.mouseInvertHorizontal != 0;
    candidate.controls.controllerAutoLookCentering = record.controllerAutoLookCentering != 0;
    candidate.controls.controllerVibration = record.controllerVibration != 0;
    candidate.controls.unidentifiedToggle = record.unidentifiedToggle != 0;
    candidate.controls.mouseAimSmoothing = record.mouseAimSmoothing != 0;
    candidate.controls.controllerSwapShoulders = record.controllerSwapShoulders != 0;
    // only setup confirmation, input prefs and FOV are imported; the rest is validated and dropped
    if (bindings.accountSeedVersion != 0 && bindings.accountSeedVersion != 1) return false;
    if (bindings.accountSeedVersion == 1) {
        // FOV can be zero before native seeding; sparse reports keep the last saved value
        if (records.fieldOfViewTouched)
            candidate.display.fieldOfView = bindings.fieldOfViewAdjustment;
        if (bindings.sourceSelector > 1) return false;
        candidate.keyBindings.seeded = true;
        candidate.keyBindings.useLocalSource = bindings.sourceSelector != 0;
        for (std::size_t slot = 0; slot < preferences::kActionsBySlot.size(); ++slot) {
            auto& binding = candidate.keyBindings.values[
                static_cast<std::size_t>(preferences::kActionsBySlot[slot])];
            const auto primary = static_cast<std::uint16_t>(bindings.keyBindings[slot]);
            const auto secondary = static_cast<std::uint16_t>(bindings.keyBindings[slot] >> 16U);
            binding.primary = primary == 0x74U ? std::nullopt : std::optional{primary};
            binding.secondary = secondary == 0x74U ? std::nullopt : std::optional{secondary};
        }
    }
    if (!state::account::settings::valid(candidate)) return false;
    output = candidate;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode701
