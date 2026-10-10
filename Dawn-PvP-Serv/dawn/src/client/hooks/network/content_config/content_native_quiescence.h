#pragma once
#include <Windows.h>
#include <cstdint>
#include <array>
#include "gate/content_config_signature_gate.h"
namespace dawn::client::hooks::network::content_config::native_work {
struct Metadata final {
    const LONG* state{};
    std::uintptr_t fetchBegin{}, fetchEnd{}, parseBegin{}, parseEnd{};
    std::uintptr_t tickBegin{}, tickEnd{};
    std::array<void*,4> trustedLeafImages{};
};
[[nodiscard]] bool resolve(std::byte* gate, Metadata& output) noexcept;
[[nodiscard]] bool stack_is_idle(CONTEXT context, const Metadata& metadata) noexcept;
[[nodiscard]] bool restore_when_idle(const Metadata& metadata, gate::Ownership& ownership) noexcept;
}
