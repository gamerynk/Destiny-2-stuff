#include <Windows.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "../dawn/src/steam/interfaces/methods/app_ticket.cpp"

namespace {
std::uint64_t simulatedTime = 1000;
std::uint64_t serial = 0;
bool signingWorks = true;
bool callbacksWork = true;
unsigned callbacks = 0;

bool sign(std::uint64_t actualTime, std::span<std::byte> output) {
    assert(actualTime > 0);
    if (!signingWorks) return false;
    ++serial;
    std::memcpy(output.data(), &simulatedTime, sizeof simulatedTime);
    std::memcpy(output.data() + 8, &serial, sizeof serial);
    return true;
}
}

namespace dawn::core::identity::device_identity {
bool initialize(void* module) noexcept { return module != nullptr; }
bool issue(Scope scope, std::uint64_t now, std::span<std::byte, kProofSize> output) noexcept {
    assert(scope == Scope::signOn);
    return sign(now, output);
}
}
namespace dawn::core::identity::local_ticket {
bool initialize(void* module) noexcept { return module != nullptr; }
bool issue(std::uint64_t platform, std::uint64_t now,
           std::span<std::byte, kTicketSize> output) noexcept {
    assert(platform == 42);
    return sign(now, output);
}
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept {}
}
namespace dawn::steam {
std::uint64_t local_steam_id() noexcept { return 42; }
ApiCall next_api_call() noexcept { static ApiCall call = 0; return ++call; }
bool queue_callback(int id, ApiCall call, const void* payload, std::size_t size) noexcept {
    assert(id == 154 && call != 0 && size == sizeof(int));
    assert(*static_cast<const int*>(payload) == 1);
    if (!callbacksWork) return false;
    ++callbacks;
    return true;
}
}

int main() {
    using namespace dawn::steam::interfaces::methods;
    using Proof = std::array<unsigned char, kTicketSize>;
    const auto read = [] {
        Proof output{};
        DWORD size = 0;
        assert(get_encrypted_app_ticket(nullptr, output.data(), static_cast<int>(output.size()), &size));
        assert(size == output.size());
        return output;
    };
    const auto accepted = [](const Proof& proof) {
        std::uint64_t issued = 0;
        std::memcpy(&issued, proof.data(), sizeof issued);
        return issued <= simulatedTime && simulatedTime - issued <= 900;
    };
    assert(request_encrypted_app_ticket(nullptr, nullptr, 0) != 0);
    auto previous = read();
    assert(accepted(previous));
    assert(read() == previous && serial == 1);
    for (int hour = 0; hour != 3; ++hour) {
        simulatedTime += 3600;
        assert(!accepted(previous));
        assert(request_encrypted_app_ticket(nullptr, nullptr, 0) != 0);
        const auto renewed = read();
        assert(accepted(renewed) && renewed != previous);
        assert(read() == renewed);
        previous = renewed;
    }
    const auto callbacksBeforeFailure = callbacks;
    signingWorks = false;
    assert(request_encrypted_app_ticket(nullptr, nullptr, 0) == 0);
    assert(callbacks == callbacksBeforeFailure);
    Proof output{};
    DWORD size = 123;
    assert(!get_encrypted_app_ticket(nullptr, output.data(), static_cast<int>(output.size()), &size));
    assert(size == 0);
    signingWorks = true;
    assert(request_encrypted_app_ticket(nullptr, nullptr, 0) != 0);
    assert(accepted(read()));
    const auto serialBeforeInvalidRead = serial;
    size = 123;
    assert(!get_encrypted_app_ticket(nullptr, output.data(), 1, &size) && size == 0);
    assert(!get_encrypted_app_ticket(nullptr, nullptr, static_cast<int>(output.size()), &size));
    assert(serial == serialBeforeInvalidRead);
    callbacksWork = false;
    assert(request_encrypted_app_ticket(nullptr, nullptr, 0) == 0);
    callbacksWork = true;
    assert(request_encrypted_app_ticket(nullptr, nullptr, 0) != 0);
    assert(accepted(read()));
    std::puts("PASS: repeated renewal, stable reads, signing failure, recovery, invalid buffers, callback failure");
}
