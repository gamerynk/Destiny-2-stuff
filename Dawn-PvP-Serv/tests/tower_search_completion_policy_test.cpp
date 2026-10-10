#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "client/hooks/bootflow/tower_search_completion_policy.h"
namespace p = dawn::client::hooks::bootflow::tower_search_completion_policy;

template <typename T, std::size_t N>
void put(std::array<std::byte, N>& bytes, std::size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof value);
}
struct Fixture {
    std::array<std::byte, p::kConfigSize> c{};
    std::array<std::byte, p::kPropertiesSize> props{};
    std::array<std::byte, p::kSearchPrefixSize> s{};
    Fixture() {
        c[0] = std::byte{1};
        put(c, 0x1F8, p::kTowerActivity); put(props, 8, p::kTowerActivity);
        props[0x2A2] = std::byte{9};
        put(c, 0x220, std::uint64_t{0x5C01}); put(props, 0, std::uint64_t{0x5C01});
        put(c, 0x21C, std::uint32_t{0xFFFFFFFF}); put(props, 0xC, std::uint32_t{0xFFFFFFFF});
        put(c, 0x28C, std::int32_t{26}); put(props, 0x28, std::int32_t{1});
        put(s, 0x10, std::uint32_t{2}); put(s, 4, std::uint16_t{5});
    }
    bool ready(bool native = true) const { return p::completion_ready(native, c, props, s); }
};

unsigned checks{};
void check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
    ++checks;
}
int main(int argc, char** argv) {
    if (argc == 3) {
        Fixture f;
        put(f.s, 4, static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 0)));
        put(f.s, 0x10, static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0)));
        std::printf("%d\n", f.ready() ? 1 : 0);
        return 0;
    }
    Fixture f;
    check(!f.ready(), "captured first query cannot complete");
    const auto before = f;
    (void)f.ready();
    check(f.c == before.c && f.props == before.props && f.s == before.s, "no native memory mutation");
    for (const std::uint16_t flags : {std::uint16_t{4}, std::uint16_t{0x10}, std::uint16_t{0x20}, std::uint16_t{0x35}}) {
        f = Fixture{}; put(f.s, 4, flags); check(!f.ready(), "request/provider/QoS/evaluation pending");
    }
    for (const std::uint16_t flags : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0x80}}) {
        f = Fixture{}; put(f.s, 4, flags);
        check(f.ready(), "empty/failed/all-unsuitable work released immediately");
        check(!f.ready(false), "native join or team wait cannot become success");
    }
    for (const std::uint32_t version : {0U, 1U, 3U, 4U, 100U, 0xFFFFFFFFU}) {
        f = Fixture{}; put(f.s, 0x10, version); check(!f.ready(), "properties version is not a lifecycle state");
    }
    for (const std::int32_t epoch : {-1, 16, 100}) {
        f = Fixture{}; put(f.s, 0x14, epoch); check(f.ready(), "invalid/stale epoch");
    }
    f = Fixture{}; put(f.s, 0x10, 3U); put(f.s, 0x14, -1); put(f.s, 4, std::uint16_t{0x21});
    check(!f.ready(), "captured version2-to3 provider-busy reset gap held");
    put(f.s, 4, std::uint16_t{0}); check(f.ready(), "actual stopped search without pending flags releases");
    f = Fixture{}; put(f.s, 0, 1U); check(f.ready(), "secondary lane unchanged");
    f = Fixture{}; put(f.c, 0x1F8, 0x12345678U); check(f.ready(), "other activity unchanged");
    f = Fixture{}; put(f.props, 8, 0x12345678U); check(f.ready(), "stale activity properties");
    f = Fixture{}; f.c[0] = std::byte{0}; check(f.ready(), "disabled configuration");
    f = Fixture{}; f.props[0x2A2] = std::byte{2}; check(f.ready(), "pending establishment");
    f = Fixture{}; f.props[0x2A2] = std::byte{0}; check(f.ready(), "invalid properties");
    f = Fixture{}; put(f.props, 0, std::uint64_t{0x1234}); check(f.ready(), "different silo");
    f = Fixture{}; put(f.props, 0xC, 123U); check(f.ready(), "different slice");
    f = Fixture{}; put(f.c, 0x248, 10); check(f.ready(), "authored nonzero window untouched");
    for (const int count : {0, 26, 27}) {
        f = Fixture{}; put(f.props, 0x28, count); check(f.ready(), "empty/full composition native answer");
    }
    f = Fixture{}; put(f.c, 0x28C, 0); check(f.ready(), "invalid capacity");
    f = Fixture{};
    check(p::completion_ready(true, {}, f.props, f.s), "missing config does not hold");
    check(p::completion_ready(true, f.c, {}, f.s), "missing properties do not hold");
    check(p::completion_ready(true, f.c, f.props, {}), "missing search does not hold");
    check(!p::completion_ready(false, {}, {}, {}), "native false always preserved");
    for (unsigned repeat = 0; repeat != 3; ++repeat) {
        put(f.s, 4, std::uint16_t{5}); check(!f.ready(), "fresh query guarded again");
        put(f.s, 4, std::uint16_t{0x11}); check(!f.ready(), "reply alone is not completion");
        put(f.s, 4, std::uint16_t{1}); check(f.ready(), "terminal evaluation releases");
    }
    for(const auto hash : {0xE8ABA41BU,0x4E0FA012U,0x5D813A4CU,0x13B3BA49U,
                          0xD873EF56U,0x4CEF34A6U,0x34B43AB2U,0x59905E37U}) {
        f=Fixture{};put(f.c,0x1F8,hash);put(f.props,8,hash);
        check(!f.ready(),"RUN94 social search cannot finish before query");
        put(f.s,4,std::uint16_t{0x21});put(f.s,0x14,-1);
        check(!f.ready(),"RUN94 social provider busy survives epoch reset");
        put(f.s,4,std::uint16_t{1});put(f.s,0x14,0);
        check(f.ready(),"RUN94 terminal search completes without injected wait");
        put(f.s,4,std::uint16_t{5});put(f.props,8,hash^1U);
        check(f.ready(),"RUN94 different social definition properties do not gate");
    }
    for(const auto hash : {0U,0x04F12A8AU,0xE8ABA41AU,0xDEE2DFE6U,0x11882989U,0x12345678U}) {
        f=Fixture{};put(f.c,0x1F8,hash);put(f.props,8,hash);
        check(f.ready(),"RUN94 Orbit, mission, Red War and unknown activity unchanged");
    }
    std::printf("PASS: %u social native-pending completion checks\n", checks);
}
