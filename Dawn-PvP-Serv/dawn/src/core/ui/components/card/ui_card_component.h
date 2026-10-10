#pragma once

#include <imgui.h>

namespace dawn::core::ui::components::card {

class [[nodiscard]] Scope final {
public:
    explicit Scope(const char* id, const ImVec2& size = {}) noexcept;

    ~Scope() noexcept;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    [[nodiscard]] bool visible() const noexcept;

private:
    bool started_{};
    bool visible_{};
};

} // namespace dawn::core::ui::components::card
