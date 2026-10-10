#include "activity_client_keepalive_validator.h"

namespace dawn::middleware::bap::activity_message::client_keepalive {

bool validate_client_keepalive(std::span<const std::byte> input) noexcept {
    return input.size() == kEncodedSize;
}

} // namespace dawn::middleware::bap::activity_message::client_keepalive
