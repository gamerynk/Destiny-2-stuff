#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "client/hooks/bootflow/entity_creation_fairness.h"

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace create = dawn::client::hooks::bootflow::entity_creation_fairness;
namespace life = dawn::client::hooks::bootflow::entity_lifecycle_fairness;
namespace move = dawn::client::hooks::bootflow::entity_movement_fairness;
namespace policy = dawn::client::hooks::bootflow::entity_movement_policy;
template<class T> void put(void* p, std::size_t at, T value) {
    std::memcpy(static_cast<std::byte*>(p) + at, &value, sizeof value);
}
bool check(bool pass, const char* message) {
    if (!pass) std::fprintf(stderr, "FAIL: %s\n", message);
    return pass;
}
struct Fixture {
    static constexpr std::uintptr_t image = 0x10000000;
    std::array<std::byte, 0xD000> manager{};
    std::array<std::byte, 0x70 * 8> rows{};
    std::array<std::array<std::byte, 0x98>, 8> aux{};
    std::array<std::byte, 0x40> lane{};
    Fixture() {
        life::imageBase = image; move::table = rows.data(); create::configured = true; create::scanCursor = 0;
        put(lane.data(), 0, image + 0x1C9ADD8);
        put(lane.data(), 9, std::uint8_t{1}); put(lane.data(), 10, std::uint8_t{1});
        put(lane.data(), 0x10, manager.data()); put(manager.data(), 0xC520, std::uint32_t{255});
        for (std::uint32_t i = 0; i < 8; ++i) {
            auto* r = row(i);
            put(manager.data(), 0x114 + i * 6, static_cast<std::int16_t>(i));
            put(r, 1, std::uint8_t{255}); put(r, 8, 0x10000U + i);
            put(r, 0xC, UINT32_MAX); put(r, 0x44, UINT32_MAX); put(r, 0x48, UINT32_MAX);
            put(r, 0x60, aux[i].data()); put(aux[i].data(), 4, std::uint32_t{1});
            put(aux[i].data(), 0x38, std::uint64_t{100}); flags(i, 0x81);
        }
    }
    std::byte* row(std::size_t i) { return rows.data() + i * 0x70; }
    void flags(std::size_t i, std::uint16_t f) { put(aux[i].data(), 0x50, f); }
    std::uint64_t age(std::uint64_t now = 350, bool valid = true, std::uint32_t channel = 0) {
        std::size_t budget = 256;
        return create::eligible_age(lane.data(), (channel << 29) | (2U << 27), {valid, now}, channel, budget);
    }
};
int main() {
    bool ok = true;
    Fixture f;
    constexpr auto packed = 2U << 27;
    auto output = packed;
    ok &= check(f.age() == 250, "local gathered creation has native age");
    ok &= check(create::apply(f.lane.data(), 0, 2, 1, 1, &output, {true, 349}) == 0
        && output == packed, "young creations retain native priority");
    ok &= check(!f.age(99) && !f.age(500, false), "future and unknown clocks are denied");
    for (std::uint32_t channel = 0; channel < 3; ++channel)
        ok &= check(f.age(350, true, channel) == 250, "all three exact gather channels supported");
    ok &= check(!f.age(350, true, 3), "unsupported channel denied");
    put(f.row(0), 1, std::uint8_t{0}); ok &= check(!f.age(), "remote owner denied");
    put(f.row(0), 1, std::uint8_t{255});
    put(f.manager.data(), 0xC520, std::uint32_t{254}); ok &= check(!f.age(), "unallocated root denied");
    put(f.manager.data(), 0xC520, std::uint32_t{255});
    put(f.row(0), 8, std::uint32_t{0x10001}); ok &= check(!f.age(), "recycled mapping denied");
    put(f.row(0), 8, std::uint32_t{0x10000});
    put(f.aux[0].data(), 4, std::uint32_t{2}); ok &= check(!f.age(), "wrong recipient denied");
    put(f.aux[0].data(), 4, std::uint32_t{1});
    for (const auto flags : {0x1U, 0x82U, 0x84U, 0x85U}) {
        f.flags(0, static_cast<std::uint16_t>(flags));
        ok &= check(!f.age(), "non-gathered, snapshot and removal roots are not creations");
    }
    f.flags(0, 0x81);
    for (const auto flags : {4U, 0x200U}) {
        put(f.row(0), 0x50, static_cast<std::uint16_t>(flags));
        ok &= check(!f.age(), "retiring or native-blocked root denied");
    }
    put(f.row(0), 0x50, std::uint16_t{0});
    put(f.lane.data(), 0xC, std::int32_t{1}); ok &= check(!f.age(), "per-peer descriptor cannot inherit other peer work");
    put(f.lane.data(), 0xC, std::int32_t{0});

    f.flags(0, 0x82); put(f.aux[0].data(), 0x38, std::uint64_t{349});
    put(f.row(0), 0x48, std::uint32_t{0x10001});
    put(f.row(1), 0xC, std::uint32_t{0x10000}); f.flags(1, 1);
    ok &= check(f.age() == 250, "overdue child creation found despite fresh root stamp and no child gather bit");
    put(f.row(1), 8, std::uint32_t{0x20001}); ok &= check(!f.age(), "stale child generation denied");
    put(f.row(1), 8, std::uint32_t{0x10001});
    put(f.row(1), 0xC, std::uint32_t{0x20000}); ok &= check(!f.age(), "foreign parent denied");
    put(f.row(1), 0xC, std::uint32_t{0x10000});
    put(f.row(1), 0x44, std::uint32_t{0x10001}); ok &= check(!f.age(), "cycle denied even after finding creation");
    put(f.row(1), 0x44, UINT32_MAX);
    std::size_t budget = 0;
    ok &= check(!create::eligible_age(f.lane.data(), packed, {true, 350}, 0, budget), "exhausted tree budget denied");
    const auto savedRows = f.rows; const auto savedAux = f.aux; const auto savedManager = f.manager;
    output = packed | (1002U << 15);
    ok &= check(create::apply(f.lane.data(), 0, 2, 1, 1, &output, {true, 350}) == 1
        && output == (packed | policy::kPriorityMask)
        && f.rows == savedRows && f.aux == savedAux && f.manager == savedManager,
        "only native output priority changes, no dependency state or ACK edits");

    Fixture batch;
    for (std::size_t i = 0; i < 8; ++i) put(batch.aux[i].data(), 0x38, std::uint64_t{100 + i});
    std::array<std::uint32_t, 8> outputs{};
    for (std::uint32_t i = 0; i < 8; ++i) outputs[i] = packed | i | (1002U << 15);
    create::scanCursor = 5;
    ok &= check(create::apply(batch.lane.data(), 0, 2, 8, 8, outputs.data(), {true, 500}) == 2
        && (outputs[0] & policy::kPriorityMask) == policy::kPriorityMask
        && (outputs[1] & policy::kPriorityMask) == policy::kPriorityMask
        && (outputs[2] & policy::kPriorityMask) == (1002U << 15),
        "oldest two creation groups win regardless of scan rotation");

    // saturated one-record packet: the native creation age curve (.98) cannot outrank movement (1022)
    std::array<std::uint32_t, 2> queue{packed | (1002U << 15), packed | 1U | policy::kMovementPriority};
    batch.flags(1, 0x82);
    const auto highest = [&]() {
        return std::max_element(queue.begin(), queue.end(), [](auto a, auto b) {
            return (a & policy::kPriorityMask) < (b & policy::kPriorityMask);
        }) - queue.begin();
    };
    ok &= check(highest() == 1, "old scheduler sends movement ahead of overdue creation");
    ok &= check(create::apply(batch.lane.data(), 0, 2, 2, 2, queue.data(), {true, 500}) == 1
        && highest() == 0 && (queue[1] & policy::kPriorityMask) == policy::kMovementPriority,
        "creation can pass recurring movement without lowering movement priority");
    const auto unchanged = queue;
    ok &= check(!create::apply(batch.lane.data(), 1, 2, 2, 2, queue.data(), {true, 500})
        && !create::apply(batch.lane.data(), 0, 1, 2, 2, queue.data(), {true, 500})
        && !create::apply(batch.lane.data(), 0, 2, 1, 2, queue.data(), {true, 500})
        && !create::apply(batch.lane.data(), 0, 2, 2, -1, queue.data(), {true, 500})
        && queue == unchanged, "wrong channel/lane/count do not alter native output");
    create::configured = false; output = packed;
    ok &= check(!create::apply(batch.lane.data(), 0, 2, 1, 1, &output, {true, 500}) && output == packed,
        "unsupported executable layout stays native");
    move::table = nullptr; life::imageBase = 0;
    if (ok) std::puts("entity creation fairness: PASS");
    return ok ? 0 : 1;
}
