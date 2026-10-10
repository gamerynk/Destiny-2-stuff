#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dawn::client::ui::weapons::catalog {
struct Choice { std::uint32_t hash{}; std::string name; };
using Choices = std::vector<Choice>;
struct Weapon {
    std::uint32_t hash{};
    std::string name, type, description;
    std::uint8_t laneCount{};
    std::array<std::uint32_t, 12> defaults{};
    std::array<std::shared_ptr<const Choices>, 12> lanes;
};
struct View { bool loading{}; std::string error; std::vector<Weapon> weapons; };
void load() noexcept;
[[nodiscard]] std::shared_ptr<const View> snapshot() noexcept;
void shutdown() noexcept;
}
