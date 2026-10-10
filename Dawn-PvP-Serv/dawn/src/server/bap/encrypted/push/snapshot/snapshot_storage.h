#pragma once

#include "snapshot.h"

namespace dawn::server::bap::encrypted::push::snapshot {

struct Reservation {
    std::size_t rawWriteOffset{};
    std::size_t compressedWriteOffset{};
    std::size_t rawClearSize{};
    std::size_t compressedClearSize{};
};

[[nodiscard]] Reservation reserve_prior(const Scratch& scratch, const Prepared& prepared) noexcept;

void clear_after(Scratch& scratch, const Reservation& reservation) noexcept;

[[nodiscard]] bool commit(const Prepared& staged, Prepared& output) noexcept;

} // namespace dawn::server::bap::encrypted::push::snapshot
