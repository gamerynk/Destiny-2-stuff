#pragma once

#include "../../../../state/investment/investment.h"
#include "../../../encoding/bit_writer.h"

namespace dawn::middleware::web_service::messages::family5 {

[[nodiscard]] bool valid(const state::Family5State& family) noexcept;

[[nodiscard]] bool write(encoding::bits::Writer& writer,
                         const state::Family5State& family) noexcept;

} // namespace dawn::middleware::web_service::messages::family5
