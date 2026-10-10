#pragma once

#include <cstdint>

#include "../definition.h"

namespace dawn::state::activity::bubble_authority {

[[nodiscard]] bool
select_grant(std::uint64_t sessionId, std::int32_t sliceSetIndex, Grant& grant) noexcept;

void record_grant(std::uint64_t sessionId, const Grant& grant) noexcept;

// a join resets the client's roster container, so old grants would leave the new one ungranted
void clear_grants(std::uint64_t sessionId) noexcept;

} // namespace dawn::state::activity::bubble_authority
