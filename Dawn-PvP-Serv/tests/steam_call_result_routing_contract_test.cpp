#include <cstdint>
#include <cstdio>

#include "steam/runtime/callbacks/callback_registry.h"

namespace {

using dawn::steam::runtime::callbacks::CallResultEntry;
using dawn::steam::runtime::callbacks::CallbackMethod;
using dawn::steam::runtime::callbacks::matches_call_result;
using dawn::steam::kSerializedRendezvousCallbackSize;
using dawn::steam::kSerializedRendezvousPacketCapacity;

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

} // namespace

int main() {
    int callbackObject{};
    constexpr dawn::steam::ApiCall kCertificateCall = 77;
    const CallResultEntry certificate{&callbackObject, kCertificateCall};

    bool passed = true;
    passed &= require(static_cast<std::size_t>(CallbackMethod::callResult) == 0,
                      "archived ABI routes API call results through vtable slot zero");
    passed &= require(static_cast<std::size_t>(CallbackMethod::regular) == 1,
                      "archived ABI routes regular callbacks through vtable slot one");
    passed &= require(static_cast<std::size_t>(CallbackMethod::payloadSize) == 2,
                      "archived ABI queries callback payload size through vtable slot two");
    passed &= require(kSerializedRendezvousPacketCapacity == 0x200U,
                      "archived rendezvous callback carries one 0x200-byte packet");
    passed &= require(kSerializedRendezvousCallbackSize == 0x210U,
                      "archived rendezvous callback advertises its exact 0x210-byte ABI size");
    passed &= require(matches_call_result(certificate, kCertificateCall),
                      "matching API-call handle routes certificate result");
    passed &= require(!matches_call_result(certificate, kCertificateCall + 1),
                      "different API-call handle is rejected");
    passed &= require(!matches_call_result(CallResultEntry{}, kCertificateCall),
                      "empty registration is rejected");
    passed &= require(!matches_call_result(certificate, 0),
                      "invalid zero API-call handle is rejected");

    if (passed) {
        std::puts("PASS: Steam call-result routing contract");
        return 0;
    }
    return 1;
}
