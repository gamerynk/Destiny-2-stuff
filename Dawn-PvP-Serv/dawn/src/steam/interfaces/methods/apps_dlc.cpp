#include <algorithm>
#include <cstdint>

#include "../../../state/entitlements/entitlement_runtime.h"
#include "../../../state/entitlements/validation.h"
#include "../internal.h"

namespace dawn::steam::interfaces::methods {
namespace {

[[nodiscard]] const state::entitlements::Table& policy() noexcept {
    return state::entitlements::get();
}

[[nodiscard]] bool entry_at(int index, std::uint32_t& identifier, std::string_view& name) noexcept {
    if (index < 0) {
        return false;
    }
    const state::entitlements::Table& table = policy();
    int remaining = index;
    for (std::size_t slot = 0; slot < table.count; ++slot) {
        if (table.entries[slot].ownership != state::entitlements::Ownership::application) {
            continue;
        }
        if (remaining == 0) {
            name = state::entitlements::name_of(table.entries[slot]);
            return state::entitlements::owned_identifier(table, slot, identifier);
        }
        --remaining;
    }
    return false;
}

[[nodiscard]] bool authored_dlc(DWORD candidate) noexcept {
    const state::entitlements::Table& table = policy();
    for (std::size_t slot = 0; slot < table.count; ++slot) {
        std::uint32_t identifier = 0;
        if (table.entries[slot].ownership == state::entitlements::Ownership::application
            && state::entitlements::owned_identifier(table, slot, identifier)
            && identifier == candidate) {
            return true;
        }
    }
    return false;
}

} // namespace

bool dlc_installed([[maybe_unused]] void* self, DWORD candidate) noexcept {
    return candidate == 0 || authored_dlc(candidate);
}

int get_dlc_count([[maybe_unused]] void* self) noexcept {
    const state::entitlements::Table& table = policy();
    const std::span<const state::entitlements::Entitlement> entries =
        state::entitlements::used(table);
    return static_cast<int>(
        std::count_if(entries.begin(), entries.end(), [](const auto& entry) noexcept {
            return entry.ownership == state::entitlements::Ownership::application;
        }));
}

bool get_dlc_data([[maybe_unused]] void* self,
                  int index,
                  DWORD* appId,
                  bool* available,
                  char* name,
                  int nameCapacity) noexcept {
    std::uint32_t identifier = 0;
    std::string_view text;
    const bool found = entry_at(index, identifier, text);
    if (appId != nullptr) {
        *appId = found ? identifier : 0;
    }
    if (available != nullptr) {
        *available = found;
    }
    if (name != nullptr && nameCapacity > 0) {
        // the buffer always ends with a null, so a short caller buffer truncates instead of
        // failing
        const auto capacity = static_cast<std::size_t>(nameCapacity) - 1;
        const std::size_t copied = found ? (std::min)(text.size(), capacity) : 0;
        std::copy_n(text.begin(), copied, name);
        name[copied] = '\0';
    }
    return found;
}

} // namespace dawn::steam::interfaces::methods
