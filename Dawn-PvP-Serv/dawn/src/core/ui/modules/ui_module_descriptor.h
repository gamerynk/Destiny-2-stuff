#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawn::core::ui::modules {

enum class Owner : std::uint8_t {
    client,
    server,
    core,
};

inline constexpr std::size_t kStableIdCapacity = 48;
inline constexpr std::size_t kDisplayNameCapacity = 64;

using FrameCallback = void (*)() noexcept;

class Descriptor final {
public:
    Descriptor() noexcept = default;

    [[nodiscard]] Owner owner() const noexcept;

    [[nodiscard]] std::string_view stable_id() const noexcept;

    [[nodiscard]] std::string_view display_name() const noexcept;

    [[nodiscard]] FrameCallback frame_callback() const noexcept;

private:
    friend bool create_descriptor(Owner owner,
                                  std::string_view stableId,
                                  std::string_view displayName,
                                  FrameCallback frameCallback,
                                  Descriptor& output) noexcept;
    friend bool is_valid(const Descriptor& descriptor) noexcept;

    Owner owner_{Owner::client};
    std::array<char, kStableIdCapacity> stableId_{};
    std::size_t stableIdLength_{};
    std::array<char, kDisplayNameCapacity> displayName_{};
    std::size_t displayNameLength_{};
    FrameCallback frameCallback_{};
};

[[nodiscard]] bool create_descriptor(Owner owner,
                                     std::string_view stableId,
                                     std::string_view displayName,
                                     FrameCallback frameCallback,
                                     Descriptor& output) noexcept;

[[nodiscard]] bool is_valid(const Descriptor& descriptor) noexcept;

} // namespace dawn::core::ui::modules
