#include "content_config_signature_gate.h"

#include <atomic>

namespace dawn::client::hooks::network::content_config::gate {
namespace {

// the signed-manifest check is skipped only while the gate byte is zero
constexpr char kDisabledValue = 0;
static_assert(std::atomic<char>::is_always_lock_free);

[[nodiscard]] char read(std::byte* address) noexcept {
    return std::atomic_ref(*reinterpret_cast<char*>(address)).load(std::memory_order_acquire);
}

} // namespace

bool disable(std::byte* address, Ownership& ownership) noexcept {
    ownership = {};
    if (address == nullptr) {
        return false;
    }
    const char previous = read(address);
    ownership.address = address;
    if (previous == kDisabledValue) {
        return true;
    }
    char expected = previous;
    if (!std::atomic_ref(*reinterpret_cast<char*>(address))
             .compare_exchange_strong(
                 expected, kDisabledValue, std::memory_order_acq_rel, std::memory_order_acquire)) {
        ownership = {};
        return false;
    }
    ownership.previous = previous;
    ownership.owned = true;
    return true;
}

bool reassert(Ownership& ownership) noexcept {
    if (ownership.address == nullptr) {
        return false;
    }
    const char previous = read(ownership.address);
    if (previous == kDisabledValue) {
        return true;
    }
    char expected = previous;
    if (!std::atomic_ref(*reinterpret_cast<char*>(ownership.address))
             .compare_exchange_strong(
                 expected, kDisabledValue, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return false;
    }
    // keep the first value we replaced so detach restores what the Client last set
    if (!ownership.owned) {
        ownership.previous = previous;
        ownership.owned = true;
    }
    return true;
}

bool restore(Ownership& ownership) noexcept {
    if (ownership.address == nullptr || !ownership.owned) {
        ownership = {};
        return true;
    }
    char expected = kDisabledValue;
    (void)std::atomic_ref(*reinterpret_cast<char*>(ownership.address))
        .compare_exchange_strong(
            expected, ownership.previous, std::memory_order_acq_rel, std::memory_order_acquire);
    ownership = {};
    return true;
}

bool is_disabled(const Ownership& ownership) noexcept {
    return ownership.address != nullptr && read(ownership.address) == kDisabledValue;
}

} // namespace dawn::client::hooks::network::content_config::gate
