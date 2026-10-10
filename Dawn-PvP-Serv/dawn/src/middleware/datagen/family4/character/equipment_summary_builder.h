#pragma once

#include "../../../../state/equipment/light/definition.h"
#include "layout.h"

namespace dawn::middleware::datagen::family4::character {

[[nodiscard]] bool build_equipment_summary(const state::equipment::light::Evaluation& evaluation,
                                           layout::EquipmentSummary& output) noexcept;

} // namespace dawn::middleware::datagen::family4::character
