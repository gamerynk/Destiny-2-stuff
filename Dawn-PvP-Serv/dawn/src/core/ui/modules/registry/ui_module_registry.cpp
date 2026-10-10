#include "ui_module_registry.h"

#include <Windows.h>

namespace dawn::core::ui::modules::registry {
namespace {

struct Slot {
    Descriptor descriptor;
    bool occupied{};
};

constexpr std::size_t kOwnerCount = 3;
constexpr std::array<Owner, kOwnerCount> kMenuOrder{Owner::client, Owner::server, Owner::core};

std::array<Slot, kModuleCapacity> g_slots{};
std::uint64_t g_revision{};
SRWLOCK g_registryLock{SRWLOCK_INIT};

[[nodiscard]] bool is_valid_owner(Owner owner) noexcept {
    return owner == Owner::client || owner == Owner::server || owner == Owner::core;
}

[[nodiscard]] bool has_id(const Slot& slot, std::string_view stableId) noexcept {
    return slot.descriptor.stable_id() == stableId;
}

} // namespace

std::span<const Descriptor> RegistrySnapshot::entries() const noexcept {
    return {entries_.data(), count_};
}

std::uint64_t RegistrySnapshot::revision() const noexcept {
    return revision_;
}

RegistrationResult register_module(const Descriptor& descriptor) noexcept {
    if (!is_valid(descriptor)) {
        return RegistrationResult::invalidDescriptor;
    }

    AcquireSRWLockExclusive(&g_registryLock);
    Slot* available = nullptr;
    for (Slot& slot : g_slots) {
        if (slot.occupied && has_id(slot, descriptor.stable_id())) {
            ReleaseSRWLockExclusive(&g_registryLock);
            return RegistrationResult::duplicateStableId;
        }
        if (!slot.occupied && available == nullptr) {
            // keep scanning after a free slot is found so duplicates are still caught
            available = &slot;
        }
    }
    if (available == nullptr) {
        ReleaseSRWLockExclusive(&g_registryLock);
        return RegistrationResult::capacityReached;
    }
    available->descriptor = descriptor;
    available->occupied = true;
    ++g_revision;
    ReleaseSRWLockExclusive(&g_registryLock);
    return RegistrationResult::registered;
}

bool unregister_module(std::string_view stableId) noexcept {
    if (stableId.empty()) {
        return false;
    }
    AcquireSRWLockExclusive(&g_registryLock);
    for (Slot& slot : g_slots) {
        if (slot.occupied && has_id(slot, stableId)) {
            slot = {};
            ++g_revision;
            ReleaseSRWLockExclusive(&g_registryLock);
            return true;
        }
    }
    ReleaseSRWLockExclusive(&g_registryLock);
    return false;
}

RegistrySnapshot snapshot() noexcept {
    RegistrySnapshot result;
    AcquireSRWLockShared(&g_registryLock);
    result.revision_ = g_revision;
    for (const Owner owner : kMenuOrder) {
        for (const Slot& slot : g_slots) {
            if (slot.occupied && slot.descriptor.owner() == owner) {
                result.entries_[result.count_++] = slot.descriptor;
            }
        }
    }
    ReleaseSRWLockShared(&g_registryLock);
    return result;
}

void clear(Owner owner) noexcept {
    if (!is_valid_owner(owner)) {
        return;
    }
    bool changed = false;
    AcquireSRWLockExclusive(&g_registryLock);
    for (Slot& slot : g_slots) {
        if (slot.occupied && slot.descriptor.owner() == owner) {
            slot = {};
            changed = true;
        }
    }
    if (changed) {
        ++g_revision;
    }
    ReleaseSRWLockExclusive(&g_registryLock);
}

void clear() noexcept {
    bool changed = false;
    AcquireSRWLockExclusive(&g_registryLock);
    for (Slot& slot : g_slots) {
        if (slot.occupied) {
            slot = {};
            changed = true;
        }
    }
    if (changed) {
        ++g_revision;
    }
    ReleaseSRWLockExclusive(&g_registryLock);
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_registryLock);
    g_slots = {};
    g_revision = 0;
    ReleaseSRWLockExclusive(&g_registryLock);
}

namespace {

// separate from the registry lock: a slot calls into the registry while holding this one
SRWLOCK g_slotLock{SRWLOCK_INIT};

} // namespace

bool PageRegistration::acquire(Owner owner,
                               std::string_view stableId,
                               std::string_view displayName,
                               FrameCallback callback,
                               void (*prepare)() noexcept) noexcept {
    AcquireSRWLockExclusive(&g_slotLock);
    if (registered_) {
        ReleaseSRWLockExclusive(&g_slotLock);
        return true;
    }
    if (prepare != nullptr) {
        prepare();
    }
    Descriptor descriptor;
    if (create_descriptor(owner, stableId, displayName, callback, descriptor)
        && register_module(descriptor) == RegistrationResult::registered) {
        stableId_ = stableId;
        registered_ = true;
    }
    const bool registered = registered_;
    ReleaseSRWLockExclusive(&g_slotLock);
    return registered;
}

void PageRegistration::release(void (*finish)() noexcept) noexcept {
    AcquireSRWLockExclusive(&g_slotLock);
    if (registered_) {
        (void)unregister_module(stableId_);
        stableId_ = {};
        registered_ = false;
    }
    if (finish != nullptr) {
        finish();
    }
    ReleaseSRWLockExclusive(&g_slotLock);
}

} // namespace dawn::core::ui::modules::registry
