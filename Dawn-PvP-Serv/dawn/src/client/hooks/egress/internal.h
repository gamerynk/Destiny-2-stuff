#pragma once

#include <array>
#include <cstddef>

#include "../../hooking/detour.h"
#include "platform/sdk.h"

namespace dawn::client::hooks::egress {

enum class HookSlot : std::size_t {
    connect,
    send,
    wsaSend,
    sendTo,
    wsaSendTo,
    wsaSendMsg,
    transmitFile,
    getAddrInfoW,
    getAddrInfoExW,
    getHostByName,
    getHostByAddress,
    getNameInfoA,
    getNameInfoW,
    dnsQueryEx,
    recvFrom,
    wsaRecvFrom,
    wsaIoctl,
    // optional compatibility fallback stays last so all required slots remain contiguous
    getAddrInfoA,
    count,
};

inline constexpr std::size_t kHookCount = static_cast<std::size_t>(HookSlot::count);

extern SRWLOCK g_lock;
extern std::array<hooking::detour::Handle, kHookCount> g_handles;

template <typename Function> [[nodiscard]] Function original(HookSlot slot) noexcept {
    const auto index = static_cast<std::size_t>(slot);
    return reinterpret_cast<Function>(g_handles[index].original);
}

} // namespace dawn::client::hooks::egress
