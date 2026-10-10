#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace dawn::client::ui::weapons::acquire_client {
enum class Status : unsigned char { idle, requesting, acquired, failed };
struct View final {
    Status status{Status::idle};
    std::array<char, 160> error{};
    std::uint64_t characterSoid{};
    std::uint64_t instanceSoid{};
    std::uint32_t definitionHash{};
    std::uint64_t revision{};
};
// a matching failed request keeps its id so the retry is safe
[[nodiscard]] bool acquire(std::uint64_t characterSoid, std::uint32_t weaponHash,
                            std::span<const std::uint32_t> plugs) noexcept;
// acquired means persisted by the server; native inventory delivery is asynchronous
[[nodiscard]] View snapshot() noexcept;
void shutdown() noexcept;
}
