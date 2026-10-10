#pragma once

#include <cstddef>
#include <span>

#include "registry.h"
#include "signature_text.h"

namespace dawn::client::patterns {

// a single-hook target belongs here, not in the shared sweep: one miss there kills the group
[[nodiscard]] std::byte* scan_main_image_unique(std::span<const PatternByte> signature,
                                                const char* name) noexcept;

[[nodiscard]] std::byte* resolve_relative(const std::byte* operand,
                                          const std::byte* nextInstruction) noexcept;

} // namespace dawn::client::patterns
