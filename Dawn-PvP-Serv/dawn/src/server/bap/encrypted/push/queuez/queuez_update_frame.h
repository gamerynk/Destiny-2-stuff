#pragma once

#include <cstddef>
#include <span>

#include "../../../../../middleware/queuez/queuez_update.h"
#include "../../internal.h"

namespace dawn::server::bap::encrypted::push::queuez_frame {

void clear_object_storage(Scratch& scratch,
                          std::size_t rawClearSize,
                          std::size_t compressedClearSize) noexcept;

[[nodiscard]] bool append(Scratch& scratch,
                          const middleware::queuez::Family& family,
                          std::size_t rawClearSize,
                          std::size_t compressedClearSize,
                          std::span<const std::byte, state::kAesKeySize> key,
                          std::span<const std::byte, state::kBapNonceSize> nonce,
                          std::span<std::byte> response,
                          std::size_t& written) noexcept;

} // namespace dawn::server::bap::encrypted::push::queuez_frame
