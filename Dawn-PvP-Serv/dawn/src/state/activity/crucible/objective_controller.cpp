#include "objective_controller.h"

#include <algorithm>
#include <array>

namespace dawn::state::activity::crucible {
namespace {

struct ObjectiveLayout final {
    std::uint32_t scenarioTag;
    std::uint32_t registryKey;
    std::array<std::uint16_t, 32> slotIndices;
};
struct RoleSchema final {
    std::uint8_t slotType;
    std::uint32_t componentClass;
    std::uint32_t authClass;
    std::uint32_t nameHash;
};
struct RoleRange final { std::size_t first, count; };
constexpr std::array<RoleRange, 12> kRoleRanges{{
    {0, 3}, {3, 2}, {5, 2}, {7, 2}, {9, 1}, {10, 2},
    {12, 2}, {14, 2}, {16, 12}, {28, 2}, {30, 1}, {31, 1},
}};

#include "objective_bindings.inc"

consteval bool valid_layouts() {
    if (kObjectiveLayouts.size() != kNativeMapCount
        || kObjectiveRoleSchemas.size() != 32) return false;
    for (std::size_t i = 0; i < kObjectiveLayouts.size(); ++i) {
        const auto& row = kObjectiveLayouts[i];
        if (row.scenarioTag == 0 || row.registryKey == 0
            || row.slotIndices[0] != 13 || row.slotIndices[3] != 27
            || row.slotIndices[10] != 34) return false;
        for (const auto slot : row.slotIndices) if (slot > 0x7FFFU) return false;
        for (std::size_t earlier = 0; earlier < i; ++earlier)
            if (kObjectiveLayouts[earlier].scenarioTag == row.scenarioTag) return false;
    }
    return true;
}
static_assert(valid_layouts());

} // namespace

std::size_t objective_role_count(ObjectiveRole role) noexcept {
    const auto index = static_cast<std::size_t>(role);
    return index < kRoleRanges.size() ? kRoleRanges[index].count : 0;
}

bool find_objective_binding(const NativeMap& map, ObjectiveRole role,
                            std::size_t ordinal, ObjectiveBinding& output) noexcept {
    output = {};
    const auto roleIndex = static_cast<std::size_t>(role);
    if (roleIndex >= kRoleRanges.size() || ordinal >= kRoleRanges[roleIndex].count)
        return false;
    const NativeMap* installed = find_native_map(map.scenarioTag);
    if (installed == nullptr || installed->scenarioName != map.scenarioName)
        return false;
    const auto found = std::find_if(kObjectiveLayouts.begin(), kObjectiveLayouts.end(),
        [&map](const ObjectiveLayout& row) { return row.scenarioTag == map.scenarioTag; });
    if (found == kObjectiveLayouts.end()) return false;
    const auto index = kRoleRanges[roleIndex].first + ordinal;
    const RoleSchema& schema = kObjectiveRoleSchemas[index];
    output = {found->registryKey, found->slotIndices[index], schema.slotType,
        schema.componentClass, schema.authClass, schema.nameHash};
    return true;
}

} // namespace dawn::state::activity::crucible
