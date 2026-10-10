#pragma once

#include "../runtime.h"

namespace dawn::state::activity::bubble_authority::transactions {

inline bool equal(const Grant& first, const Grant& second) noexcept {
    return first.bubble == second.bubble && first.token == second.token;
}

inline bool equal(const AuthorityState& first, const AuthorityState& second) noexcept {
    return first.grantTokens == second.grantTokens;
}

} // namespace dawn::state::activity::bubble_authority::transactions
