#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::state::activity::coo {
enum class SceneEvent : std::uint8_t { ignored, accepted, overflow };

template<class CommandBody, class Owner, std::size_t Scenes>
class SceneService final {
    static_assert(Scenes > 0 && std::is_trivially_copyable_v<CommandBody> && std::is_trivially_copyable_v<Owner>);
public:
    [[nodiscard]] const std::array<CommandBody, Scenes>& commands() const noexcept { return commands_; }
    [[nodiscard]] bool requested(std::size_t index) const noexcept { return index < Scenes && commands_[index].generation != 0; }
    [[nodiscard]] Owner owner(std::size_t index) const noexcept { return index < Scenes ? owners_[index] : Owner{}; }
    [[nodiscard]] bool begin(std::size_t index, std::uint64_t generation, const Owner& token) noexcept {
        if (index >= Scenes || generation == 0 || generation > 0x7FFFFFFFULL || !token.valid()) { return false; }
        commands_[index] = {static_cast<std::uint32_t>(generation), false, 0, {}};
        owners_[index] = token; milestones_[index] = 0; return true;
    }
    [[nodiscard]] SceneEvent event(std::size_t index, std::uint32_t eventId) noexcept {
        if (!requested(index)) { return SceneEvent::ignored; }
        auto& command = commands_[index];
        for (std::uint8_t i = 0; i < command.eventCount; ++i) { if (command.events[i] == eventId) { return SceneEvent::accepted; } }
        if (command.eventCount >= command.events.size()) { return SceneEvent::overflow; }
        command.events[command.eventCount++] = eventId; return SceneEvent::accepted;
    }
    void stop(std::size_t index) noexcept { if (index < Scenes) { commands_[index].stop = true; } }
    [[nodiscard]] bool seen(std::size_t index, std::uint8_t bit) const noexcept { return index < Scenes && (milestones_[index] & bit) != 0; }
    void mark(std::size_t index, std::uint8_t bit) noexcept {
        if (index < Scenes) { milestones_[index] = static_cast<std::uint8_t>(milestones_[index] | bit); }
    }
private:
    std::array<CommandBody, Scenes> commands_{};
    std::array<Owner, Scenes> owners_{};
    std::array<std::uint8_t, Scenes> milestones_{};
};
} // namespace dawn::state::activity::coo
