#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace dawn::steam::runtime::native_network_diagnostics {
struct NativeDiagnosticEvent {
    std::uint64_t generation{}, sequence{}, observedTickMs{};
    int level{};
    std::size_t length{};
    std::array<char, 1201> text{};
};

// multi-producer, one externally serialized consumer; no allocation or waiting. One CAS
// bounds producer work; a full ring drops the diagnostic. Never reset while callbacks fly
template<std::size_t Capacity = 128> class NativeDiagnosticQueue {
    static_assert(Capacity >= 2);
    static_assert(std::atomic_uint64_t::is_always_lock_free);
    struct Slot {
        std::atomic_uint64_t publication{};
        NativeDiagnosticEvent event{};
    };
    std::array<Slot, Capacity> slots_{};
    std::atomic_uint64_t enqueue_{};
    std::uint64_t dequeue_{};
public:
    std::atomic_uint64_t rejected{};
    NativeDiagnosticQueue() noexcept {
        for (std::size_t index = 0; index < Capacity; ++index)
            slots_[index].publication.store(index, std::memory_order_relaxed);
    }
    bool try_push(int level, std::uint64_t sequence, std::uint64_t generation,
                  std::string_view text, std::uint64_t observedTickMs = 0) noexcept {
        auto position = enqueue_.load(std::memory_order_relaxed);
        auto& slot = slots_[position % Capacity];
        if (slot.publication.load(std::memory_order_acquire) != position
            || !enqueue_.compare_exchange_strong(position, position + 1, std::memory_order_relaxed)) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto& event = slot.event;
        event.level = level;
        event.sequence = sequence;
        event.generation = generation;
        event.observedTickMs = observedTickMs;
        event.length = text.size() < event.text.size() ? text.size() : event.text.size() - 1;
        std::memcpy(event.text.data(), text.data(), event.length);
        event.text[event.length] = '\0';
        slot.publication.store(position + 1, std::memory_order_release);
        return true;
    }
    bool try_pop(NativeDiagnosticEvent& event) noexcept {
        auto& slot = slots_[dequeue_ % Capacity];
        if (slot.publication.load(std::memory_order_acquire) != dequeue_ + 1) return false;
        event = slot.event;
        slot.publication.store(dequeue_ + Capacity, std::memory_order_release);
        ++dequeue_;
        return true;
    }
};
} // namespace dawn::steam::runtime::native_network_diagnostics
