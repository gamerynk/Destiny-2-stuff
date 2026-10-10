#pragma once

#include <cstddef>
#include <cstdint>

#include "native_maps.h"
#include "../coo/native_device_authority.h"

namespace dawn::state::activity::crucible {

enum class ObjectiveRole : std::uint8_t {
    captureZone,
    demolitionPlant,
    demolitionBomb,
    breakthroughBase,
    breakthroughNeutral,
    allegianceEffect,
    allegianceFilter,
    sideInfluence,
    teamDirective,
    teamFilter,
    supremacyEffect,
    supremacyFilter,
};

struct ObjectiveBinding final {
    std::uint32_t registryKey{};
    std::uint16_t slotIndex{};
    std::uint8_t slotType{};
    std::uint32_t componentClass{};
    std::uint32_t authClass{};
    std::uint32_t nameHash{};
};

[[nodiscard]] bool find_objective_binding(const NativeMap& map,
    ObjectiveRole role, std::size_t ordinal, ObjectiveBinding& output) noexcept;
[[nodiscard]] std::size_t objective_role_count(ObjectiveRole role) noexcept;

// type-4 schema 0x8080992F: placement generation and presence only, no capture ownership or
// progress; check the mode requests this placement before publishing
template<class Writer>
[[nodiscard]] bool write_placed_object(Writer& writer, const NativeMap& map,
    ObjectiveRole role, std::size_t ordinal, std::uint32_t generation,
    bool present) noexcept {
    ObjectiveBinding binding{};
    return generation != 0 && generation < 0x7FFFFFFFU
        && find_objective_binding(map, role, ordinal, binding)
        && binding.slotType == 4 && binding.componentClass == 0x80809927U
        && binding.authClass == 0x8080992FU
        && coo::native_device::object(writer, generation, present);
}

struct ObjectiveScopedRef final {
    std::uint32_t registryKey{};
    std::uint8_t slotType{};
    std::uint16_t slotIndex{};
    bool present{};
};

inline constexpr std::size_t kSideSelectorBodyBits = 32 + 7 + 16 + 2;

// enum values are left unnamed because side-influence behaviour is unproven; keep this
// out of live mode selection until its consumer is traced
template<class Writer>
[[nodiscard]] bool write_side_selector_raw(Writer& writer, const NativeMap& map,
    std::size_t ordinal, ObjectiveScopedRef target, std::int8_t enumValue) noexcept {
    ObjectiveBinding binding{};
    if (!find_objective_binding(map, ObjectiveRole::sideInfluence, ordinal, binding)
        || binding.slotType != 32 || binding.componentClass != 0x80809556U
        || binding.authClass != 0x8080955AU
        || enumValue < -1 || enumValue > 2
        || (target.present && (target.registryKey == 0
            || target.registryKey == 0x811C9DC5U || target.slotType > 126
            || target.slotIndex > 0x7FFFU))) {
        return false;
    }
    const std::uint32_t registry = target.present ? target.registryKey : 0x811C9DC5U;
    const std::uint32_t type = target.present ? target.slotType + 1U : 0U;
    const std::uint32_t slot = target.present ? target.slotIndex + 32768U : 32767U;
    return writer.write(registry, 32) && writer.write(type, 7)
        && writer.write(slot, 16)
        && writer.write(static_cast<std::uint32_t>(enumValue + 1), 2);
}

} // namespace dawn::state::activity::crucible
