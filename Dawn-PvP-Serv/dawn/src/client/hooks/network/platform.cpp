#include "platform.h"
#include "../../../steam/runtime/transport/native_certificate_publish.h"

#include "coordinator/network_call_coordinator.h"

namespace dawn::client::hooks::network::platform {
namespace {

std::atomic_bool g_automaticNative{false};

using AuthenticationStatus = int(__fastcall*)(void*, void*);
// a failed certificate request can still allow native local P2P/poll-group setup; cached
// valid certs already report 100, the fresh-cache fallback needs this mapping
constexpr int kAvailabilityCurrent = 100;
constexpr int kAvailabilityFailed = -101;
__declspec(noinline) int __fastcall authentication_status_body(void* self, void* details) noexcept {
    coordinator::CallLease lease{};
    coordinator::g_callIngress(lease, HookSlot::authenticationStatus);
    const auto call = reinterpret_cast<AuthenticationStatus>(lease.original);
    const int value = call != nullptr ? call(self, details) : kAvailabilityFailed;
    const int result = lease.accepting && value == kAvailabilityFailed ? kAvailabilityCurrent : value;
    coordinator::g_callEgress();
    return result;
}

using SetCertificate = bool(__fastcall*)(void*, const void*, int, char*);
bool native_certificate_policy() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        if (GetEnvironmentVariableW(L"DAWN_NATIVE_STEAM_CERTIFICATE", value, 2) == 1)
            return value[0] == L'1';
#if defined(RESTORATION_NATIVE_STEAM_CERTIFICATE) && RESTORATION_NATIVE_STEAM_CERTIFICATE
        return true;
#else
        return false;
#endif
    }();
    return enabled;
}

__declspec(noinline) bool __fastcall set_certificate_body(void* self,
                                                          const void* certificate,
                                                          int certificateSize,
                                                          char* error) noexcept {
    coordinator::CallLease lease{};
    coordinator::g_callIngress(lease, HookSlot::setCertificate);
    const bool result = [&]() noexcept {
        const auto call = reinterpret_cast<SetCertificate>(lease.original);
        if (call == nullptr) {
            return false;
        }
        const bool accepted = call(self, certificate, certificateSize, error);
        // native-certificate trials omit this detour in platform_slots(); default owner builds
        // keep the historical Boolean override. Publication alone proves no CA acceptance
        const bool probe = native_certificate_policy();
        const bool nativeReturn = probe && steam::runtime::transport::native_certificate_publish::published();
        return lease.accepting && !nativeReturn ? true : accepted;
    }();
    coordinator::g_callEgress();
    return result;
}

} // namespace

bool native_certificate_requested() noexcept {
    return native_certificate_policy();
}

bool native_authentication_requested() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        bool requested{};
        if (GetEnvironmentVariableW(L"DAWN_NATIVE_STEAM_AUTHENTICATION", value, 2) == 1)
            requested = value[0] == L'1';
        else {
#if defined(RESTORATION_NATIVE_STEAM_AUTHENTICATION) && RESTORATION_NATIVE_STEAM_AUTHENTICATION
            requested = true;
#endif
        }
        return requested && native_certificate_policy();
    }();
    return enabled;
}

bool automatic_native_selected() noexcept { return g_automaticNative.load(std::memory_order_acquire); }
void select_automatic_native(bool selected) noexcept { g_automaticNative.store(selected, std::memory_order_release); }

bool automatic_native_ready() noexcept {
    // native publication is optional: unsupported SDK layouts, owner/offline identity, read-only
    // server storage, old empty ACKs and expiring keys keep the compatibility group
    const auto read = [](std::uintptr_t address, void* output, std::size_t size) noexcept {
        SIZE_T received{};
        return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            output, size, &received) != FALSE && received == size;
    };
    const HMODULE module = GetModuleHandleW(L"steamnetworkingsockets.dll");
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto accessor = module == nullptr ? nullptr : GetProcAddress(module, "SteamNetworkingSockets_Lib");
    if (accessor == nullptr || !steam::runtime::transport::native_certificate_publish::durably_published()) return false;
    // public accessor used by the native rendezvous path; never call a guessed game RVA
    using Accessor = void* (*)();
    void* self = reinterpret_cast<Accessor>(accessor)();
    const auto object = reinterpret_cast<std::uintptr_t>(self);
    std::uintptr_t vtable{}, method{};
    if (!read(object, &vtable, sizeof vtable) || vtable != base + 0x24BC40
        || !read(vtable + 0xB0, &method, sizeof method) || method != base + 0xD7740) return false;
    constexpr std::array<unsigned char, 12> kStatusTail{0x8B,0x9F,0xA0,0x04,0,0,0xE8,0xBD,0xE7,0x01,0,0x90};
    std::array<unsigned char, kStatusTail.size()> statusTail{};
    if (!read(base + 0xD77C8, statusTail.data(), statusTail.size()) || statusTail != kStatusTail) return false;
    coordinator::CallLease lease{};
    coordinator::g_callIngress(lease, HookSlot::authenticationStatus);
    const auto call = reinterpret_cast<AuthenticationStatus>(lease.original);
    bool ready = false;
    if (call != nullptr && call(self, nullptr) == kAvailabilityCurrent) {
        // read only the SDK's public protobuf string, never private-key storage; ReadProcessMemory
        // avoids touching a freeing allocation, and a second read rejects concurrent replacement
        std::uintptr_t certificate{}, after{};
        std::array<std::uintptr_t, 4> string{}, finalString{};
        std::array<std::byte, 512> publicBytes{};
        if (read(object + 0x28, &certificate, sizeof certificate)
            && read(certificate, string.data(), sizeof string)
            && string[2] > 0 && string[2] <= publicBytes.size() && string[3] >= string[2]
            && read(string[3] >= 16 ? string[0] : certificate, publicBytes.data(), string[2])
            && read(object + 0x28, &after, sizeof after) && after == certificate
            && read(certificate, finalString.data(), sizeof finalString) && string == finalString
            && call(self, nullptr) == kAvailabilityCurrent) {
            ready = steam::runtime::transport::native_certificate_publish::durable_certificate_matches(
                std::span(publicBytes).first(string[2]));
        }
    }
    coordinator::g_callEgress();
    return ready;
}

void* authentication_status_entry_point() noexcept {
    return reinterpret_cast<void*>(&authentication_status_body);
}

void* set_certificate_entry_point() noexcept {
    return reinterpret_cast<void*>(&set_certificate_body);
}

} // namespace dawn::client::hooks::network::platform
