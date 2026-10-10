#pragma once

#include "../../../../logging/view/log_snapshot_view.h"

namespace dawn::core::ui::modules::logs::internal::filters {

void draw_channel() noexcept;

void draw_level() noexcept;

void draw_text() noexcept;

[[nodiscard]] log::view::Filter current() noexcept;

void reset() noexcept;

} // namespace dawn::core::ui::modules::logs::internal::filters
