#pragma once

#include <cstdint>

#include "../../../../middleware/bap/activity_message/definition.h"

namespace dawn::server::bap::encrypted::activity_message::mission_sense {

// only a link bound to the session the mission host serves is read, so another fireteam's reports can't drive this run
void observe_sense_update(std::uint64_t boundSessionId,
                          const middleware::bap::activity_message::Request& request) noexcept;

void observe_incident(std::uint64_t boundSessionId,
                      const middleware::bap::activity_message::Request& request) noexcept;

} // namespace dawn::server::bap::encrypted::activity_message::mission_sense
