#pragma once

#include <cstddef>
#include <cstdint>

#include "../../patterns/image_scan.h"

namespace dawn::client::hooks::teleport {

using NativePhysicsHandle = std::uint32_t*(__fastcall*)(std::uint32_t*, std::uint32_t);
struct NativePlayerBodyLookup {
    NativePhysicsHandle getter{};
    const std::byte* poolPointer{};
    const std::byte* unitsPointer{};
    const std::byte* unitStride{};
    const std::byte* unitCapacity{};
};
void publish_native_player_lookup(NativePlayerBodyLookup lookup) noexcept;

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

// object-handle index bits: two handles name the same object exactly when these match
inline constexpr std::uint32_t kHandleIndexMask = 0x1FFF;
// written by the controlled-object getter when the local player owns no object
inline constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFF;

inline constexpr std::size_t kPhysicsComponentObjectHandle = 44;

inline constexpr std::size_t kPhysicsComponentBodyArray = 400;
inline constexpr std::size_t kPhysicsComponentBodyIndex = 516;
inline constexpr std::size_t kBodyEntryStride = 80;
inline constexpr std::size_t kBodyPointer = 32;

// rigid-body world position; still a hypothesis, lined up through a third-party Havok layout
inline constexpr std::size_t kBodyPositionX = 448;

} // namespace dawn::client::hooks::teleport
