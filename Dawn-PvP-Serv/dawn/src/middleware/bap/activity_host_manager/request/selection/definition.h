#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_host_manager::request::selection {

inline constexpr std::size_t kActivityManagerSkullCapacity = 16;
inline constexpr std::size_t kActivityManagerPackageNameCapacity = 40;
// fits the biggest encoding: 1,814 bits with every dynamic count maxed, 372 for the
// common form
inline constexpr std::size_t kActivityManagerDescriptorCapacity = 232;

struct ActivityManagerSkullValue final {
    std::int8_t group{};
    std::int8_t value{};
};

struct ActivityManagerSelection final {
    struct MenuOption { std::int16_t index{-1}, choice{-1}; };
    std::array<MenuOption,63> menuOptions{};
    std::uint8_t menuOptionCount{};
    std::int8_t requestKind{};
    std::int8_t reason{};
    std::int16_t sourceActivityIndex{};
    std::int16_t activityIndex{};
    bool hasElementIndex{};
    std::int16_t elementIndex{};
    std::uint8_t skullCount{};
    std::array<ActivityManagerSkullValue, kActivityManagerSkullCapacity> skulls{};
    bool hasArrivalBubbleHash{};
    std::uint32_t arrivalBubbleHash{};
    bool hasSpawnSetHash{};
    std::uint32_t spawnSetHash{};
    bool hasPackageName{};
    std::uint8_t packageNameLength{};
    std::array<std::int8_t, kActivityManagerPackageNameCapacity> packageName{};
    std::size_t packageNameBitOffset{};
    bool trailingFlag{};
    // the client's exact bits, left-aligned; the host echoes them back and they carry unnamed
    // fields that re-encoding from the scalars would drop
    std::array<std::byte, kActivityManagerDescriptorCapacity> descriptorBits{};
    std::size_t descriptorBitLength{};
};

struct ActivityManagerSelectionResult final {
    bool hasSelection{};
    ActivityManagerSelection selection{};
    bool hasSecondary{};
    ActivityManagerSelection secondary{};
};

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
