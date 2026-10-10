#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace dawn::client::hooks::teleport {
inline constexpr std::size_t kVectorLanes = 3;
using Vector = std::array<float, kVectorLanes>;
using ControlledHandle = std::uint32_t* (*)(std::uint32_t*);
void publish_targets(ControlledHandle controlled) noexcept;
void clear_targets() noexcept;
[[nodiscard]] bool install() noexcept;
[[nodiscard]] bool uninstall() noexcept;
[[nodiscard]] bool owns_local_player(void* component) noexcept;
[[nodiscard]] bool read_position(void* component, Vector& position) noexcept;
[[nodiscard]] void* local_player_component() noexcept;
} // namespace dawn::client::hooks::teleport
