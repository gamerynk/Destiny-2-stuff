#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <set>
#include "../dawn/src/steam/runtime/native_network_diagnostics.cpp"

namespace dawn::core::log {
std::vector<std::string> events;
void write(Channel, Level, std::string_view event) noexcept { events.emplace_back(event); }
}
namespace {
unsigned previousCalls{}, relayCalls{}, setterCalls{};
unsigned qualityCalls{};
int restoredLevel{};
HANDLE previousEntered{}, previousRelease{};
using namespace dawn::steam::runtime::native_network_diagnostics;
void previous(int, const char*) { ++previousCalls; }
void other(int, const char*) {}
void previous_paused(int, const char*) {
    SetEvent(previousEntered);
    assert(WaitForSingleObject(previousRelease, 5000) == WAIT_OBJECT_0);
}
int relay(void*, void* details) { assert(details == nullptr); ++relayCalls; return 100; }
void set(void*, int level, Output callback) {
    ++setterCalls;
    restoredLevel = level;
    assert(callback == &previous || callback == nullptr);
}
bool quick(void*, std::uint32_t handle, void* storage) {
    ++qualityCalls;
    if (handle == 42) return false;
    const int state = 3, ping = 75, pending = 1200;
    const float quality = .97F;
    const std::int64_t queue = 10000;
    std::memcpy(static_cast<std::byte*>(storage), &state, sizeof state);
    std::memcpy(static_cast<std::byte*>(storage) + 4, &ping, sizeof ping);
    std::memcpy(static_cast<std::byte*>(storage) + 8, &quality, sizeof quality);
    std::memcpy(static_cast<std::byte*>(storage) + 40, &pending, sizeof pending);
    std::memcpy(static_cast<std::byte*>(storage) + 48, &queue, sizeof queue);
    return true;
}
}
int main() {
    using namespace dawn::steam::runtime::native_network_diagnostics;
    g_previous.store(&previous);
    g_previousLevel.store(3);
    output(3, "native receive warning");
    output(5, "native connection diagnostic");
    assert(previousCalls == 1 && g_lines.load() == 2);
    assert(dawn::core::log::events.empty());
    Output native = &output;
    g_nativeOutput = &native;
    g_utils = reinterpret_cast<void*>(1);
    g_relayStatus = &relay;
    g_setOutput = &set;
    service(0);
    assert(relayCalls == 0);
    start();
    service(1);
    service(2);
    service(5001);
    assert(relayCalls == 2);
    assert(dawn::core::log::events.back().find("availability=100 changed=0") != std::string::npos);
    shutdown();
    assert(setterCalls == 1 && restoredLevel == 3);
    output(3, "already in flight at shutdown");
    assert(previousCalls == 2);
    g_nativeOutput = &native;
    native = &other;
    g_setOutput = &set;
    shutdown();
    assert(setterCalls == 1);
    native = &output;
    g_nativeOutput = &native;
    g_utils = reinterpret_cast<void*>(1);
    g_relayStatus = &relay;
    g_setOutput = &set;
    g_previous.store(nullptr);
    g_previousLevel.store(0);
    start();
    service(10000);
    assert(relayCalls == 3);
    shutdown();
    assert(setterCalls == 2 && restoredLevel == 0);
    std::uint32_t parsed = 987;
    for (const auto text : {"[# P2P steamid:1] connected", "[#0 P2P steamid:1] connected",
            "[#4294967296 P2P steamid:1] connected", "[#12 UDP steamid:1] connected",
            "[#12 P2P steamid:1 connected", "prefix [#12 P2P steamid:1] connected"}) {
        assert(!parse_native_p2p_debug_id(text, parsed) && parsed == 987);
    }
    assert(parse_native_p2p_debug_id("[#4294967295 P2P steamid:1] connected", parsed)
        && parsed == UINT32_MAX);
    NativeConnectionDiagnosticHandles bounded;
    for (unsigned id = 1; id <= 16; ++id) {
        const auto text = "[#" + std::to_string(id) + " P2P steamid:1] connected";
        bounded.observe(text);
        bounded.observe(text);
    }
    bounded.observe("[#17 P2P steamid:1] connected");
    assert(bounded.capacityMisses.load() == 1);
    bounded.observe("[#8 P2P steamid:1] closed by peer");
    bounded.observe("[#17 P2P steamid:1] connected");
    const auto handles = bounded.snapshot();
    assert(std::count(handles.begin(), handles.end(), 17U) == 1);
    assert(std::count(handles.begin(), handles.end(), 8U) == 0);
    assert(bounded.capacityMisses.load() == 1);
    start();
    const auto beforeDeferred = dawn::core::log::events.size();
    const auto deferredSequence = g_lines.load() + 1;
    output(5, "deferred native diagnostic");
    assert(dawn::core::log::events.size() == beforeDeferred);
    service_connection_quality(1);
    assert(dawn::core::log::events.back().find("message_sequence=" + std::to_string(deferredSequence)) != std::string::npos);
    g_qualitySockets = reinterpret_cast<void*>(1);
    g_quickStatus = &quick;
    output(5, "[#41 P2P steamid:1] connected");
    output(5, "[#42 P2P steamid:1] connected");
    service_connection_quality(20000);
    assert(qualityCalls == 2 && g_qualityLookupMisses == 1);
    const auto qualityLine = std::find_if(dawn::core::log::events.begin(), dawn::core::log::events.end(),
        [](const auto& value) { return value.find("source=connection_quality result=sample connection=41") != std::string::npos; });
    assert(qualityLine != dawn::core::log::events.end());
    assert(qualityLine->find("ping_ms=75") != std::string::npos
        && qualityLine->find("pending_reliable_bytes=1200") != std::string::npos
        && qualityLine->find("queue_time_us=10000") != std::string::npos);
    service_connection_quality(20001);
    assert(qualityCalls == 2);
    service_connection_quality(25000);
    assert(qualityCalls == 3);
    output(5, "[#41 P2P steamid:1] closed by peer");
    service_connection_quality(30000);
    assert(qualityCalls == 3);
    shutdown();
    service_connection_quality(35000);
    assert(qualityCalls == 3);
    NativeDiagnosticQueue<4> queue;
    NativeDiagnosticEvent event;
    const std::string longText(1300, 'x');
    for (unsigned index = 0; index < 4; ++index)
        assert(queue.try_push(4, index, 17, longText));
    assert(!queue.try_push(4, 99, 17, "full") && queue.rejected.load() == 1);
    for (unsigned index = 0; index < 4; ++index) {
        assert(queue.try_pop(event) && event.sequence == index && event.generation == 17);
        assert(event.length == 1200 && event.text[1200] == '\0' && event.text[1199] == 'x');
    }
    assert(!queue.try_pop(event));
    assert(queue.try_push(5, 42, 18, "reused") && queue.try_pop(event)
        && std::string_view(event.text.data(), event.length) == "reused");
    NativeDiagnosticQueue<16> concurrent;
    std::atomic_uint done{};
    std::atomic_uint64_t admitted{};
    std::array<std::thread, 4> producers;
    for (std::size_t producer = 0; producer < producers.size(); ++producer) {
        producers[producer] = std::thread([&, producer] {
            for (std::uint64_t index = 0; index < 2000; ++index) {
                const auto identity = producer * 2000 + index;
                const auto text = std::to_string(identity);
                if (concurrent.try_push(static_cast<int>(producer), identity, 22, text))
                    admitted.fetch_add(1, std::memory_order_relaxed);
            }
            done.fetch_add(1, std::memory_order_release);
        });
    }
    std::set<std::uint64_t> received;
    for (;;) {
        if (concurrent.try_pop(event)) {
            assert(event.generation == 22 && event.sequence < 8000);
            assert(std::string_view(event.text.data(), event.length) == std::to_string(event.sequence));
            assert(received.insert(event.sequence).second);
        } else if (done.load(std::memory_order_acquire) == producers.size()) break;
        else std::this_thread::yield();
    }
    for (auto& producer : producers) producer.join();
    while (concurrent.try_pop(event)) {
        assert(event.generation == 22 && event.sequence < 8000);
        assert(std::string_view(event.text.data(), event.length) == std::to_string(event.sequence));
        assert(received.insert(event.sequence).second);
    }
    assert(received.size() == admitted.load() && received.size() + concurrent.rejected.load() == 8000);
    start();
    const auto oldGeneration = g_debugGeneration.load();
    const auto beforeLifetime = dawn::core::log::events.size();
    assert(g_debugQueue.try_push(5, 9999, oldGeneration, "old generation must be discarded"));
    shutdown();
    start();
    service_connection_quality(40000);
    assert(dawn::core::log::events.size() == beforeLifetime);
    output(5, "new generation remains observable");
    assert(dawn::core::log::events.size() == beforeLifetime);
    service_connection_quality(40001);
    assert(dawn::core::log::events.size() == beforeLifetime + 1);
    previousEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    previousRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(previousEntered && previousRelease);
    g_previous.store(&previous_paused);
    g_previousLevel.store(3);
    std::thread inFlight([] { output(3, "old callback paused across shutdown and start"); });
    assert(WaitForSingleObject(previousEntered, 5000) == WAIT_OBJECT_0);
    shutdown();
    start();
    SetEvent(previousRelease);
    inFlight.join();
    g_previous.store(nullptr);
    service_connection_quality(45000);
    assert(dawn::core::log::events.size() == beforeLifetime + 1);
    CloseHandle(previousEntered);
    CloseHandle(previousRelease);
    shutdown();
    std::puts("PASS: native diagnostics preserve prior callback, verbosity and later ownership");
    std::puts("PASS: bounded exact P2P debug parsing, numeric quality fields, cadence, retirement and shutdown");
    std::puts("PASS: deferred callback logging, bounded overflow, concurrent payload publication and generation lifetime");
}
