#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace dawn::steam::runtime::native_network_diagnostics {
// debug ids are only candidates: GetQuickConnectionStatus must validate each handle
inline bool parse_native_p2p_debug_id(std::string_view message, std::uint32_t& id) noexcept {
    constexpr std::string_view prefix = "[#";
    if (!message.starts_with(prefix)) return false;
    std::uint32_t value{};
    std::size_t index = prefix.size();
    const auto first = index;
    while (index < message.size() && message[index] >= '0' && message[index] <= '9') {
        const auto digit = static_cast<std::uint32_t>(message[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        ++index;
    }
    if (index == first || value == 0 || !message.substr(index).starts_with(" P2P ")) return false;
    if (message.find(']', index + 5) == std::string_view::npos) return false;
    id = value;
    return true;
}

// native debug callbacks can run under the transport lock, so tracking never waits on a
// lock
struct NativeConnectionDiagnosticHandles {
    std::array<std::atomic_uint32_t, 16> entries{};
    std::atomic_uint64_t capacityMisses{};
    void clear() noexcept {
        for (auto& entry : entries) entry.store(0, std::memory_order_relaxed);
        capacityMisses.store(0, std::memory_order_relaxed);
    }
    void erase(std::uint32_t id) noexcept {
        for (auto& entry : entries) {
            auto expected = id;
            (void)entry.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
        }
    }
    void observe(std::string_view message) noexcept {
        std::uint32_t id{};
        if (!parse_native_p2p_debug_id(message, id)) return;
        if (message.find("closed by peer") != std::string_view::npos
            || message.find("problem detected locally") != std::string_view::npos) {
            erase(id);
            return;
        }
        for (auto& entry : entries) if (entry.load(std::memory_order_relaxed) == id) return;
        for (auto& entry : entries) {
            std::uint32_t empty{};
            if (entry.compare_exchange_strong(empty, id, std::memory_order_relaxed)) return;
            if (empty == id) return;
        }
        capacityMisses.fetch_add(1, std::memory_order_relaxed);
    }
    std::array<std::uint32_t, 16> snapshot() const noexcept {
        std::array<std::uint32_t, 16> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = entries[index].load(std::memory_order_relaxed);
        return result;
    }
};
} // namespace dawn::steam::runtime::native_network_diagnostics
