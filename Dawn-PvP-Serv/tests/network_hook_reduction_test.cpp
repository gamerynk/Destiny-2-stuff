#include <Windows.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include "core/logging/log.h"

#include "client/hooks/network/lifecycle/network_hook_entries.h"
#include "client/hooks/network/coordinator/network_call_coordinator.h"
#include "client/targets/game/network.h"
#include "client/targets/steam_targets.h"

namespace {
using namespace dawn::client;
std::array<std::byte, 4> targetStorage{};
targets::game::network::Targets gameTargets{};
targets::steam::Targets steamTargets{};
std::array<void*, hooks::network::kHandleCount> originals{};
bool accepting = true;
int entered = 0, exited = 0;
int availability = -101, parserCalls = 0;
bool parserAccepted = false;
bool certificatePublished = false;
void* expectedSelf{};
const void* expectedCertificate{};
int expectedSize{};
char* expectedError{};

int __fastcall nativeStatus(void* self, void* details) {
    assert(self == expectedSelf && details == expectedError);
    return availability;
}
bool __fastcall nativeCertificate(void* self, const void* bytes, int size, char* error) {
    ++parserCalls;
    assert(self == expectedSelf && bytes == expectedCertificate);
    assert(size == expectedSize && error == expectedError);
    error[0] = 'x';
    return parserAccepted;
}
void ingress(hooks::network::coordinator::CallLease& lease, hooks::network::HookSlot slot) noexcept {
    ++entered;
    lease.original = originals[static_cast<std::size_t>(slot)];
    lease.accepting = accepting;
}
void egress() noexcept { ++exited; }
}

namespace dawn::client::targets::game::network {
const Targets& get() noexcept { return gameTargets; }
}
namespace dawn::client::targets::steam {
const Targets& get() noexcept { return steamTargets; }
}

namespace dawn::client::hooks::network::coordinator {
const CallIngress g_callIngress = &ingress;
const CallEgress g_callEgress = &egress;
void* ingress_entry_point() noexcept { return reinterpret_cast<void*>(&ingress); }
void* egress_entry_point() noexcept { return reinterpret_cast<void*>(&egress); }
}
namespace dawn::steam::runtime::transport::native_certificate_publish {
bool published() noexcept { return certificatePublished; }
bool durably_published() noexcept { return false; }
bool durable_certificate_matches(std::span<const std::byte>) noexcept { return false; }
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept {}
}

int main(int argc, char**) {
    using namespace dawn::client::hooks::network;
    steamTargets.authenticationStatus = &targetStorage[2];
    steamTargets.setCertificate = &targetStorage[3];
    const auto steam = lifecycle::platform_specs();
    const auto selected = lifecycle::platform_slots();
    assert(selected.size() == (argc > 2 ? 0U : argc > 1 ? 1U : 2U));
    if (!selected.empty()) assert(selected.front() == HookSlot::authenticationStatus);
    if (argc == 1) assert(selected.back() == HookSlot::setCertificate);
    assert(steam.size() == 2);
    assert(steam[0].target == &targetStorage[2]);
    assert(steam[1].target == &targetStorage[3]);
    const auto protectedSteam = lifecycle::platform_protected_entries();
    for (std::size_t i = 0; i < steam.size(); ++i)
        assert(protectedSteam[i].address == steam[i].replacement);

    int object = 0;
    char certificate[4]{}, error[32]{};
    expectedSelf = &object;
    expectedCertificate = certificate;
    expectedSize = sizeof certificate;
    expectedError = error;
    originals[static_cast<std::size_t>(HookSlot::authenticationStatus)] = reinterpret_cast<void*>(&nativeStatus);
    originals[static_cast<std::size_t>(HookSlot::setCertificate)] =
        reinterpret_cast<void*>(&nativeCertificate);
    const auto setCertificate = reinterpret_cast<bool(__fastcall*)(void*, const void*, int, char*)>(
        platform::set_certificate_entry_point());
    const auto status = reinterpret_cast<int(__fastcall*)(void*, void*)>(platform::authentication_status_entry_point());
    for (int value : {-102, -101, 0, 100}) {
        availability = value; accepting = true;
        assert(status(&object, error) == (value == -101 ? 100 : value));
        accepting = false; assert(status(&object, error) == value);
    }
    accepting = true;
    assert(setCertificate(&object, certificate, sizeof certificate, error));
    assert(parserCalls == 1 && error[0] == 'x');
    accepting = false;
    assert(!setCertificate(&object, certificate, sizeof certificate, error));
    parserAccepted = true;
    assert(setCertificate(&object, certificate, sizeof certificate, error));
    assert(parserCalls == 3 && entered == exited);
    if (argc > 1) {
        certificatePublished = true; parserAccepted = false; accepting = true;
        assert(!setCertificate(&object, certificate, sizeof certificate, error));
        parserAccepted = true;
        assert(setCertificate(&object, certificate, sizeof certificate, error));
        certificatePublished = false; parserAccepted = false;
        assert(setCertificate(&object, certificate, sizeof certificate, error));
        assert(parserCalls == 6 && entered == exited);
    }
    std::puts("PASS: reduced target alignment, protected entries, local auth fallback and certificate side effects");
}
