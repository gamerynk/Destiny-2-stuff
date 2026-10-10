#pragma once

#include <span>

namespace dawn::core::ui::hud::store {

struct Switch {
    const char* key{};
    bool on{};
};

void initialize(void* module) noexcept;

void shutdown() noexcept;

void load(std::span<Switch> switches) noexcept;

bool save(std::span<const Switch> switches) noexcept;

} // namespace dawn::core::ui::hud::store
