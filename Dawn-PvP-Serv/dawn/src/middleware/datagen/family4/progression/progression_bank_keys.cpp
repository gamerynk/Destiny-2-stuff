#include "progression_bank_keys.h"

#include <array>
#include <type_traits>

#include "../../../../state/build_data/runtime.h"
#include "../../../../state/unlocks/unlocks_runtime.h"

namespace dawn::middleware::datagen::family4::progression {
namespace {

constexpr std::size_t kBankCapacity = 127;
// all bits set is the only value the record enumerator treats as an empty slot
constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFF;

// one lane type for the authored bank and the native row, so no conversion can lose a lane
static_assert(std::is_same_v<layout::Values, state::unlocks::ProgressionLanes>);

[[nodiscard]] const state::unlocks::ProgressionBank&
authored_lanes(state::build_data::progressions::Scope scope) noexcept {
    const state::unlocks::Table& table = state::unlocks::get();
    return scope == state::build_data::progressions::Scope::account ? table.accountProgressions
                                                                    : table.characterProgressions;
}

} // namespace

bool key_bank(state::build_data::progressions::Scope scope,
              std::span<layout::Entry> bank) noexcept {
    for (layout::Entry& entry : bank) {
        entry = layout::Entry{};
        entry.definitionIndex = kEmptyDefinitionIndex;
    }
    std::array<std::uint16_t, kBankCapacity> slots{};
    std::size_t count = 0;
    if (!state::build_data::find_progression_slots(scope, slots, count) || count > bank.size()) {
        return false;
    }
    const state::unlocks::ProgressionBank& lanes = authored_lanes(scope);
    for (std::size_t slot = 0; slot < count; ++slot) {
        // the definition catalog is dense, so every key it hands out addresses the authored bank
        bank[slot].definitionIndex = slots[slot];
        bank[slot].values = lanes[slots[slot]];
    }
    return true;
}

} // namespace dawn::middleware::datagen::family4::progression
