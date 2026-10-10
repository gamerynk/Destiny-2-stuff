#pragma once

#include <cstddef>

namespace dawn::server::bap::encrypted::push::queuez_report {

inline constexpr int kNoRecordOutcome = -1;

void push(const char* stage,
          unsigned family,
          std::size_t objects,
          std::size_t bytes,
          int recorded) noexcept;

void subscription_failure(const char* step) noexcept;

void subscription_state(const char* step) noexcept;

} // namespace dawn::server::bap::encrypted::push::queuez_report
