#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"

namespace dawn::state {
namespace {

using namespace runtime::detail;
namespace authored_inventory = account::inventory;
namespace family4_loadout = middleware::datagen::family4::loadout;

constexpr std::uint32_t kMaximumSerial =
    static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());

void report_vault(std::string_view stage,
                  std::string_view result,
                  std::string_view reason,
                  const PendingVaultTransfer& mutation,
                  std::uint64_t instanceSoid) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=vault stage=%.*s result=%.*s reason=%.*s direction=%s character=0x%llX "
        "instance=0x%llX definition_hash=0x%08X vault_items=%zu inventory_items=%zu",
        static_cast<int>(stage.size()),
        stage.data(),
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        mutation.toVault ? "to_vault" : "from_vault",
        static_cast<unsigned long long>(mutation.characterSoid),
        static_cast<unsigned long long>(instanceSoid),
        mutation.movedItem.definitionHash,
        mutation.after.vault.count,
        mutation.characterIndex < mutation.after.characterCount
            ? mutation.after.characters[mutation.characterIndex].inventory.count
            : 0);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

// holds two whole account images, so it is cleared in place with no temporary
void clear(PendingVaultTransfer& mutation) noexcept {
    mutation.prepared = false;
    mutation.toVault = false;
    mutation.accountSoid = 0;
    mutation.characterSoid = 0;
    mutation.instanceSoid = 0;
    mutation.characterIndex = 0;
    mutation.movedItem = {};
}

[[nodiscard]] std::int32_t greatest_account_serial(const AccountState& account) noexcept {
    std::int32_t greatest = 0;
    for (std::size_t index = 0; index < account.profileItemCount; ++index) {
        greatest = (std::max)(greatest, account.profileItems[index].mutationSerial);
    }
    for (std::size_t index = 0; index < account.vault.count; ++index) {
        greatest = (std::max)(greatest, account.vault.values[index].mutationSerial);
    }
    return greatest;
}

[[nodiscard]] bool same_account_items(const AccountState& left, const AccountState& right) noexcept {
    if (left.primarySoid != right.primarySoid || left.characterCount != right.characterCount
        || left.vault.count != right.vault.count
        || !same_profile_views(
            left.profileItems, left.profileItemCount, right.profileItems, right.profileItemCount)) {
        return false;
    }
    for (std::size_t index = 0; index < left.characterCount; ++index) {
        if (!same_character(left.characters[index], right.characters[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.vault.count; ++index) {
        if (!same_stationary_item(left.vault.values[index], right.vault.values[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool stage_vault_transfer(const AccountState& account,
                                        std::size_t characterIndex,
                                        std::uint64_t instanceSoid,
                                        bool toVault,
                                        PendingVaultTransfer& mutation) noexcept {
    clear(mutation);
    mutation.toVault = toVault;
    if (instanceSoid == 0 || !account::valid(account) || characterIndex >= account.characterCount
        || !account.characters[characterIndex].selected) {
        report_vault("stage", "fail", "selection", mutation, instanceSoid);
        return false;
    }
    const CharacterState& before = account.characters[characterIndex];
    mutation.characterSoid = before.soid;
    mutation.characterIndex = characterIndex;

    // staged on the per-thread image, not the stack: an account is several hundred kilobytes
    AccountState& candidate = mutation.after;
    candidate = account;
    CharacterState& after = candidate.characters[characterIndex];

    thread_local family4_loadout::ResolvedLoadout beforeLoadout{};
    if (!family4_loadout::resolve(account, characterIndex, beforeLoadout)
        || before.nextInventorySerial > kMaximumSerial) {
        report_vault("stage", "fail", "before_loadout", mutation, instanceSoid);
        return false;
    }

    if (toVault) {
        std::size_t found = before.inventory.count;
        for (std::size_t index = 0; index < before.inventory.count; ++index) {
            if (before.inventory.values[index].instanceSoid == instanceSoid) {
                found = index;
                break;
            }
        }
        if (found >= before.inventory.count) {
            report_vault("stage", "fail", "not_in_inventory", mutation, instanceSoid);
            return false;
        }
        const std::int32_t greatest = greatest_account_serial(account);
        if (candidate.vault.count >= candidate.vault.values.size()
            || greatest == (std::numeric_limits<std::int32_t>::max)()) {
            report_vault("stage", "fail", "vault_full", mutation, instanceSoid);
            return false;
        }
        mutation.movedItem = before.inventory.values[found];
        for (std::size_t index = found; index + 1U < after.inventory.count; ++index) {
            after.inventory.values[index] = after.inventory.values[index + 1U];
        }
        --after.inventory.count;
        after.inventory.values[after.inventory.count] = {};
        authored_inventory::Item stored = mutation.movedItem;
        stored.mutationSerial = greatest + 1;
        candidate.vault.values[candidate.vault.count++] = stored;
    } else {
        std::size_t found = account.vault.count;
        for (std::size_t index = 0; index < account.vault.count; ++index) {
            if (account.vault.values[index].instanceSoid == instanceSoid) {
                found = index;
                break;
            }
        }
        if (found >= account.vault.count) {
            report_vault("stage", "fail", "not_in_vault", mutation, instanceSoid);
            return false;
        }
        if (after.inventory.count >= after.inventory.values.size()
            || after.nextInventorySerial >= kMaximumSerial) {
            report_vault("stage", "fail", "inventory_full", mutation, instanceSoid);
            return false;
        }
        mutation.movedItem = account.vault.values[found];
        // every vault row after the pulled one moves up, so each gets a fresh serial
        std::int32_t serial = greatest_account_serial(account);
        if (static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)() - serial)
            < account.vault.count) {
            report_vault("stage", "fail", "vault_serial", mutation, instanceSoid);
            return false;
        }
        for (std::size_t index = found; index + 1U < candidate.vault.count; ++index) {
            candidate.vault.values[index] = candidate.vault.values[index + 1U];
            candidate.vault.values[index].mutationSerial = ++serial;
        }
        --candidate.vault.count;
        candidate.vault.values[candidate.vault.count] = {};
        authored_inventory::Item placed = mutation.movedItem;
        placed.mutationSerial = static_cast<std::int32_t>(after.nextInventorySerial++);
        after.inventory.values[after.inventory.count++] = placed;
    }

    thread_local family4_loadout::ResolvedLoadout placedLoadout{};
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, characterIndex, placedLoadout)) {
        // a pull into a full native bucket (e.g. a tenth kinetic weapon) lands here
        report_vault("stage", "fail", "after_loadout", mutation, instanceSoid);
        return false;
    }
    std::uint16_t movedRow = 0;
    std::uint8_t movedSlot = 0;
    if (toVault == find_unequipped_row(placedLoadout, instanceSoid, movedRow, movedSlot)) {
        report_vault("stage", "fail", "placement", mutation, instanceSoid);
        return false;
    }

    for (std::size_t index = 0; index < after.inventory.count; ++index) {
        authored_inventory::Item& survivor = after.inventory.values[index];
        if (survivor.instanceSoid == instanceSoid) {
            continue;
        }
        std::uint16_t beforeRow = 0;
        std::uint16_t afterRow = 0;
        std::uint8_t beforeSlot = 0;
        std::uint8_t afterSlot = 0;
        if (!find_unequipped_row(beforeLoadout, survivor.instanceSoid, beforeRow, beforeSlot)
            || !find_unequipped_row(placedLoadout, survivor.instanceSoid, afterRow, afterSlot)
            || beforeSlot != afterSlot || after.nextInventorySerial >= kMaximumSerial) {
            report_vault("stage", "fail", "survivor_rows", mutation, instanceSoid);
            return false;
        }
        if (beforeRow != afterRow) {
            survivor.mutationSerial = static_cast<std::int32_t>(after.nextInventorySerial++);
        }
    }
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, characterIndex, placedLoadout)) {
        report_vault("stage", "fail", "checked_loadout", mutation, instanceSoid);
        return false;
    }

    mutation.before = account;
    mutation.accountSoid = account.primarySoid;
    mutation.instanceSoid = instanceSoid;
    mutation.prepared = true;
    return true;
}

[[nodiscard]] bool materialize_vault_transfer(const AccountState& current,
                                              const PendingVaultTransfer& mutation,
                                              AccountState& after) noexcept {
    if (!mutation.prepared || mutation.accountSoid == 0 || mutation.instanceSoid == 0
        || current.primarySoid != mutation.accountSoid
        || mutation.characterIndex >= current.characterCount
        || !same_account_items(current, mutation.before)) {
        return false;
    }
    thread_local PendingVaultTransfer canonical{};
    if (!stage_vault_transfer(current,
                              mutation.characterIndex,
                              mutation.instanceSoid,
                              mutation.toVault,
                              canonical)
        || !same_account_items(canonical.after, mutation.after)) {
        return false;
    }
    after = current;
    after.characters = mutation.after.characters;
    after.vault = mutation.after.vault;
    return account::valid(after);
}

} // namespace

bool prepare_vault_transfer(std::uint64_t instanceSoid, PendingVaultTransfer& mutation) noexcept {
    thread_local AccountState account{};
    account = account_snapshot();
    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    bool inVault = false;
    for (std::size_t index = 0; index < account.vault.count; ++index) {
        inVault = inVault || account.vault.values[index].instanceSoid == instanceSoid;
    }
    if (!stage_vault_transfer(account, characterIndex, instanceSoid, !inVault, mutation)) {
        clear(mutation);
        return false;
    }
    report_vault("prepare", "ok", "ready", mutation, instanceSoid);
    return true;
}

bool preview_vault_transfer(const PendingVaultTransfer& mutation, AccountState& after) noexcept {
    thread_local AccountState current{};
    current = account_snapshot();
    const bool ready = materialize_vault_transfer(current, mutation, after);
    if (!ready) {
        report_vault("preview", "fail", "stale_or_invalid", mutation, mutation.instanceSoid);
    }
    return ready;
}

bool commit_vault_transfer(PendingVaultTransfer& mutation) noexcept {
    thread_local AccountState candidate{};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const bool ready =
        materialize_vault_transfer(runtime::storage::account(), mutation, candidate);
    const bool committed = ready && runtime::storage::publish_account_locked(candidate);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    report_vault("commit",
                 committed ? "ok" : "fail",
                 committed ? "published" : (ready ? "persist" : "stale_or_invalid"),
                 mutation,
                 mutation.instanceSoid);
    clear(mutation);
    return committed;
}

} // namespace dawn::state
