#pragma once

#include "../../core/logging/log.h"
#include "../../middleware/web_service/messages/opcode701.h"
#include "../../state/account/settings/settings_runtime.h"
#include "../../state/runtime/runtime.h"

namespace dawn::server::web_service {

// the client already applies opcode 701 locally; ack only after durable storage
// don't resend a full account object, it would overwrite unrelated client-owned fields
[[nodiscard]] inline bool save_settings(const middleware::web_service::Message& message,
    std::span<std::byte> response, std::size_t& written) noexcept {
    namespace ws = middleware::web_service;
    const auto account = state::account_snapshot();
    auto after = account.settings;
    ws::StatusResponse status{};
    status.code = 1;
    if (!ws::encode_response(message, ws::ResponseShape::statusPair, status, response, written))
        return false;
    bool changed = false;
    const bool saved = ws::messages::opcode701::parse_settings(message, account.settings, after)
        && state::account::settings::save(account.primarySoid, account.settings, after, changed);
    if (saved) status.code = 0;
    if (saved && after.profileSetupCompleted != account.settings.profileSetupCompleted) {
        core::log::write(core::log::Channel::server, core::log::Level::info,
            after.profileSetupCompleted
                ? "ev=profile_setup stage=completion result=committed value=1"
                : "ev=profile_setup stage=completion result=committed value=0");
    }
    if (!saved || changed) {
        core::log::write(core::log::Channel::server,
            saved ? core::log::Level::info : core::log::Level::warn,
            saved ? "ev=settings_save opcode=701 result=committed"
                  : "ev=settings_save opcode=701 result=refused");
    }
    return ws::encode_response(message, ws::ResponseShape::statusPair, status, response, written);
}

} // namespace dawn::server::web_service
