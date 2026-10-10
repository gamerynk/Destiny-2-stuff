#include "activity_membership_acknowledgement_parser.h"

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message::membership_acknowledgement {

bool parse_membership_acknowledgement(std::span<const std::byte> input,
                                      MembershipAcknowledgement& acknowledgement) noexcept {
    acknowledgement = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    acknowledgement.membershipRevision = encoding::read_u32_be(input.first<kEncodedSize>());
    return true;
}

} // namespace
