#pragma once
#include "middleware/bap/activity_message/peer_reservation.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {
// scalar-only: credentials, directories and opaque startup bytes are never retained
struct StartupReservations {
    bool present{};
    activity_message::peer_reservation::Request members{};
};
[[nodiscard]] bool parse_startup_reservations(std::span<const std::byte> protobuf,
                                             StartupReservations& result) noexcept;
}
