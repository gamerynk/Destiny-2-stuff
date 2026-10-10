#pragma once

#include <cstdint>

#include "../../middleware/web_service/messages/opcode405.h"
#include "../../middleware/web_service/web_service_envelope.h"
#include "web_service_runtime.h"

namespace dawn::server::web_service {

void select_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void transfer_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
// 802 and unmapped entries are refused; a plain success made the client wait for an update and drop to Weasel
void select_socket_entry(const middleware::web_service::Message& message,
                         Outcome& outcome) noexcept;
void mutate_equipment(const middleware::web_service::Message& message,
                      bool unequip,
                      Outcome& outcome) noexcept;
void mutate_socket_plug(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void mutate_equipped_socket_plug(const middleware::web_service::Message& message,
                                 Outcome& outcome) noexcept;
void mutate_item_state(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void dismantle_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void acquire_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;

} // namespace dawn::server::web_service
