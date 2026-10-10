#include "runtime.h"
#include "storage/internal.h"

namespace dawn::state {

bool begin_character_selection(std::uint64_t authenticatedAccountSoid) noexcept {
    if (authenticatedAccountSoid == 0) return false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto& account = runtime::storage::account();
    const bool owned = account.primarySoid == authenticatedAccountSoid
                       && account.characterCount <= account.characters.size();
    if (owned) {
        // selection is session state; database loading clears it, so a new generation reusing
        // this runtime record must match
        for (auto& character : account.characters) character.selected = false;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return owned;
}

} // namespace dawn::state
