#include "signon_ownership_encoder.h"

#include <algorithm>
#include <cstdint>

#include "../../../state/entitlements/validation.h"
#include "../internal.h"

namespace dawn::middleware::signon::ownership {
namespace {

constexpr unsigned kIdentifierField = 1;

} // namespace

bool encode(const state::entitlements::Table& entitlements,
            std::span<std::byte> output,
            std::size_t& size) noexcept {
    size = 0;
    Writer writer(output);
    const std::size_t rows = std::min(entitlements.count, entitlements.entries.size());
    for (std::size_t index = 0; index < rows; ++index) {
        std::uint32_t identifier = 0;
        if (!state::entitlements::owned_identifier(entitlements, index, identifier)) {
            continue;
        }
        if (!writer.varint(kIdentifierField, identifier)) {
            return false;
        }
    }
    size = writer.size();
    return true;
}

} // namespace dawn::middleware::signon::ownership
