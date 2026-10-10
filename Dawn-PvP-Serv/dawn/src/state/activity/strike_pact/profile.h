#pragma once
#include "bindings.h"
namespace dawn::state::activity::strike_pact {
inline constexpr auto& kProfile=kNativeBindings;
[[nodiscard]] bool valid_document(const coo::script::Views&) noexcept;
}
