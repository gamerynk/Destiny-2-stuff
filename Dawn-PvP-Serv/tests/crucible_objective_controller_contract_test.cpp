#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "state/activity/crucible/native_maps.h"
#include "state/activity/crucible/objective_controller.h"

namespace c = dawn::state::activity::crucible;

struct RecordingWriter final {
    std::vector<std::pair<std::uint64_t, unsigned>> fields;
    bool write(std::uint64_t value, unsigned bits) {
        if (bits == 0 || bits > 64 || (bits < 64 && value >= (1ULL << bits)))
            return false;
        fields.emplace_back(value, bits);
        return true;
    }
    std::size_t bit_count() const {
        std::size_t count = 0;
        for (const auto& field : fields) count += field.second;
        return count;
    }
};

int main() {
    assert(c::native_maps().size() == 31);
    for (const c::NativeMap& map : c::native_maps()) {
        assert(!c::combat_spawns(map).empty());
        for (unsigned role = 0; role < 12; ++role) {
            const auto named = static_cast<c::ObjectiveRole>(role);
            const auto count = c::objective_role_count(named);
            assert(count != 0);
            for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
                c::ObjectiveBinding binding{};
                assert(c::find_objective_binding(map, named, ordinal, binding));
                assert(binding.registryKey == c::combat_spawns(map).front().registryKey);
                assert(binding.slotIndex <= 0x7FFFU && binding.authClass != 0);
                assert(binding.nameHash != 0);
            }
            c::ObjectiveBinding invalid{};
            assert(!c::find_objective_binding(map, named, count, invalid));
            assert(invalid.authClass == 0);
        }
    }
    const c::NativeMap* javelin = c::find_native_map("pvp_shaft");
    const c::NativeMap* burnout = c::find_native_map("pvp_vex_tube_2");
    assert(javelin && burnout);
    c::ObjectiveBinding control{};
    c::ObjectiveBinding directive{};
    assert(c::find_objective_binding(*javelin, c::ObjectiveRole::captureZone, 0, control));
    assert(control.slotIndex == 13 && control.slotType == 4);
    assert(c::find_objective_binding(*burnout, c::ObjectiveRole::teamDirective, 0,
                                     directive));
    assert(directive.registryKey == 0xD28FF232U && directive.slotType == 68);
    RecordingWriter placed;
    assert(c::write_placed_object(placed, *javelin,
        c::ObjectiveRole::captureZone, 0, 1, true));
    assert(placed.bit_count() == 252);
    RecordingWriter wrongRole;
    assert(!c::write_placed_object(wrongRole, *javelin,
        c::ObjectiveRole::teamDirective, 0, 1, true));
    assert(wrongRole.fields.empty());
    RecordingWriter side;
    assert(c::write_side_selector_raw(side, *javelin, 0, {}, -1));
    assert(side.bit_count() == c::kSideSelectorBodyBits);
    assert(side.fields.size() == 4);
    assert((side.fields[0] == std::pair<std::uint64_t, unsigned>{0x811C9DC5U, 32}));
    assert((side.fields[1] == std::pair<std::uint64_t, unsigned>{0, 7}));
    assert((side.fields[2] == std::pair<std::uint64_t, unsigned>{32767, 16}));
    assert((side.fields[3] == std::pair<std::uint64_t, unsigned>{0, 2}));
    RecordingWriter invalidSide;
    assert(!c::write_side_selector_raw(invalidSide, *javelin, 0, {}, 3));
    assert(invalidSide.fields.empty());
    std::puts("PASS: installed Crucible objective controller bindings and bodies");
}
