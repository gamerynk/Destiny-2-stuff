#pragma once

#include "../web_service_envelope.h"
#include "../../../state/account/settings/settings_state.h"

namespace dawn::middleware::web_service::messages::opcode701 {

inline constexpr std::uint16_t kOpcode = 701;

[[nodiscard]] bool parse_settings(const Message& message,
    const state::account::settings::AccountSettings& before,
    state::account::settings::AccountSettings& output) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode701
