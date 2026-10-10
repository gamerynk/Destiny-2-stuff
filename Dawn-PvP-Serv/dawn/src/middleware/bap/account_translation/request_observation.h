#pragma once

#include "../../encoding/byte_order.h"
#include "account_translation_response.h"

namespace dawn::middleware::bap::account_translation {

struct RequestObservation {
    const char* reason{"short_header"};
    std::uint16_t count{};
    std::uint8_t typeA{};
    std::uint8_t typeB{};
    std::uint64_t firstIdentity{};
};

[[nodiscard]] inline RequestObservation observe_request(
    std::span<const std::byte> body) noexcept {
    RequestObservation result{};
    if (body.size() < 4) {
        return result;
    }
    result.count = encoding::read_u16_be(body.first<2>());
    result.typeA = std::to_integer<std::uint8_t>(body[2]);
    result.typeB = std::to_integer<std::uint8_t>(body[3]);
    // only scalar identity selectors have a qword here; never mislabel text bytes
    if (result.count != 0 && body.size() >= 12
        && (result.typeA == 12 || result.typeA == 255)) {
        result.firstIdentity = encoding::read_u64_be(body.subspan<4, 8>());
    }
    result.reason = (result.typeA != 12 || result.typeB != 255) ? "unsupported_type"
                    : result.count > kMaximumEntries ? "too_many_entries"
                    : result.count == 0 ? "empty_list"
                    : body.size() < 4U + static_cast<std::size_t>(result.count) * 8U
                        ? "truncated_list"
                    : body.size() > 4U + static_cast<std::size_t>(result.count) * 8U
                        ? "trailing_bytes"
                    : result.count > 1 ? "valid_batch"
                    : result.firstIdentity == 0 ? "zero_identity"
                                                           : "valid_scalar";
    return result;
}

} // namespace dawn::middleware::bap::account_translation
