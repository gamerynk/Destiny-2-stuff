#include <algorithm>

#include "entity_slots.h"

namespace dawn::middleware::bap::activity_message::entity_slots {

bool decode_entity_slots(std::span<const std::byte> input, EntitySlotMask& mask) noexcept {
    mask = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    std::copy_n(input.begin(), kEncodedSize, mask.begin());
    return true;
}

} // namespace dawn::middleware::bap::activity_message::entity_slots
