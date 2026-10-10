#pragma once
#include "../../../middleware/social/realtime_codec.h"
namespace dawn::steam::runtime::social::rendezvous_sender {
bool start() noexcept;
void shutdown() noexcept;
bool enqueue(const middleware::social::realtime::Request& request) noexcept;
}
