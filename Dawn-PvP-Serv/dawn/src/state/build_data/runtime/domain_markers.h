#pragma once

namespace dawn::state::build_data::runtime {

namespace ability_buckets {

void clear() noexcept;

void publish() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace ability_buckets

namespace details {

void clear() noexcept;

void publish() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace details

namespace name_catalog {

void clear() noexcept;

void publish() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace name_catalog

namespace spawn_catalog {

void clear() noexcept;

void publish() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace spawn_catalog

namespace named {

void clear() noexcept;

void publish() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace named

} // namespace dawn::state::build_data::runtime
