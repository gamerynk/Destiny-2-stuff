#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

#include "client/hooks/bootflow/entity_movement_fairness.h"
#include "client/hooks/bootflow/combat_feedback_priority.h"

namespace {

namespace movement = dawn::client::hooks::bootflow::entity_movement_fairness;
namespace policy = dawn::client::hooks::bootflow::entity_movement_policy;
namespace feedback = dawn::client::hooks::bootflow::combat_feedback_priority;

bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

template <class T, std::size_t N>
void put(std::array<std::byte, N>& storage, std::size_t offset, const T& value) noexcept {
    std::memcpy(storage.data() + offset, &value, sizeof value);
}

struct NativeFixture {
    std::array<std::byte, 0x70> row{};
    std::array<std::byte, 0x40> pool{};
    std::array<std::byte, 0x20> allocator{};
    std::array<std::byte, 0x20> bitmap{};
    std::array<std::byte, 128> bits{};
    std::array<std::byte, 0x4000> records{};
    std::array<std::byte, 0x100> manager{};
    std::array<std::byte, 0x150> indices{};
    std::array<std::byte, 0x100> auxiliary{};
    std::array<std::byte, 16> header{};
    std::array<std::byte, 32> lane{};

    NativeFixture() noexcept {
        put(row, 1, std::uint8_t{0xFF});
        put(row, 2, std::uint16_t{48});
        put(row, 4, std::uint32_t{0x00800001});
        put(row, 8, std::uint32_t{0x00100004});
        put(row, 0x2C, std::uint16_t{16});
        put(row, 0x2E, std::uint16_t{48});
        put(row, 0x30, header.data());
        put(row, 0x60, auxiliary.data());
        put(header, 0, std::uint32_t{0x80000000});
        put(pool, 0, allocator.data());
        put(pool, 8, records.data());
        put(pool, 0x10, std::uint32_t{12});
        put(pool, 0x14, std::uint32_t{1024});
        put(pool, 0x1C, std::uint32_t{0x3000});
        put(pool, 0x20, std::uint32_t{4});
        put(pool, 0x30, std::uint32_t{0x80806ACC});
        put(allocator, 8, bitmap.data());
        put(bitmap, 8, std::uint32_t{1024});
        put(bitmap, 16, bits.data());
        put(bits, 0, std::uint32_t{2});
        put(records, 0x3004, std::uint8_t{1});
        put(records, 16, std::uint32_t{1});
        put(auxiliary, 4, std::uint32_t{1});
        put(auxiliary, 0x18 + 0x10, std::uint32_t{1});
        put(auxiliary, 0x18 + 0x20, std::uint64_t{100});
        put(auxiliary, 0x18 + 0x38, std::uint16_t{0x82});
        put(lane, 9, std::uint8_t{1});
        put(lane, 10, std::uint8_t{1});
        put(lane, 0x10, indices.data());
        put(indices, 0x114 + 4 * 6, std::int16_t{0});
        movement::sobjectPool.store(pool.data());
    }
};

struct EventFixture {
    static constexpr std::uintptr_t image = 0x10000000;
    static constexpr std::uint32_t packet = (1U << 29) | (3U << 27) | 5U;
    std::array<std::byte, 0x38> lane{};
    std::array<std::byte, 0xAC0> manager{};
    std::array<std::byte, 16> clock{};
    std::array<std::byte, 0x20> domain{};
    std::array<std::byte, 0x130> registry{};
    std::array<std::byte, 8> definition{}, unrelated{};
    std::array<std::byte, 0x78> vtable{}, otherVtable{};
    std::array<std::byte, 0x164> head{}, child{};
    EventFixture() noexcept {
        feedback::imageBase.store(image);
        put(clock, 0, std::uint8_t{1});
        put(clock, 8, std::uint64_t{200});
        movement::clockRoot.store(clock.data());
        put(lane, 0, image + 0x1C9AE50);
        put(lane, 0xA, std::uint8_t{1});
        put(lane, 0xC, std::uint32_t{7});
        put(lane, 0x20, manager.data());
        put(manager, 8, domain.data());
        put(domain, 0x18, registry.data());
        put(registry, 0x30 + 13 * 8, definition.data());
        put(registry, 0x30 + 10 * 8, unrelated.data());
        put(definition, 0, vtable.data());
        put(unrelated, 0, otherVtable.data());
        put(vtable, 0x70, image + 0xB7D710);
        put(manager, 0x9D0 + 5 * 8, head.data());
        put(head, 4, std::uint32_t{13});
        put(head, 8, std::int32_t{-1});
        put(head, 0x10, std::uint64_t{100});
        put(head, 0x18, std::int32_t{-1});
        put(child, 0x18, std::int32_t{-1});
        put(head, 0x50, std::uint32_t{1U << 7});
        put(head, 0x60, std::uint32_t{5});
    }
};

} // namespace

int main() {
    bool passed = true;
    int lane{}, manager{}, descriptor{};
    const policy::Key key{&lane, &manager, &descriptor, 0x00100004, 0};
    policy::Policy state{};
    passed &= require(!state.observe(key, 100, 1000, {true, 149}).overdue,
                      "first observation retains native priority");
    passed &= require(!state.observe(key, 100, 1001, {true, 149}).overdue,
                      "native age below 50 ms is not boosted");
    passed &= require(state.observe(key, 100, 1002, {true, 150}).overdue,
                      "stale eligible update reaches native 50 ms cadence");
    state.mark_boost(key);
    const auto progressed = state.observe(key, 150, 1003, {true, 151});
    passed &= require(progressed.stampAdvanced && progressed.advancedAfterBoost && !progressed.overdue,
                      "actual send stamp progress clears overdue state and attributes boost");
    passed &= require(!state.observe(key, 150, 1004, {true, 152}).advancedAfterBoost,
                      "one stamp advance is not counted repeatedly");
    passed &= require(!state.observe(key, 149, 1005, {true, 153}).stampAdvanced,
                      "backward native stamp is not send progress");
    passed &= require(!state.observe(key, 149, 4006, {true, 5000}).overdue,
                      "continuity gap cannot inherit an old urgent descriptor");
    passed &= require(!state.observe(key, 149, 4005, {true, 5000}).overdue,
                      "OS clock rewind resets continuity");
    state = {};
    (void)state.observe(key, 100000, 5000, {true, 1});
    passed &= require(!state.observe(key, 100000, 5249, {true, 2}).overdue
                          && state.observe(key, 100000, 5250, {true, 3}).overdue,
                      "future native stamp uses observed 250 ms stagnation, never mixed clocks");
    auto replacement = key;
    ++replacement.token;
    passed &= require(!state.observe(replacement, 100000, 5251).overdue,
                      "recycled token starts a new tracking interval");
    state.begin(&lane, 0);
    state.finish(&lane, 0);
    passed &= require(!state.observe(key, 100000, 5502).overdue,
                      "disappeared candidate cannot keep old urgency");
    auto otherChannel = key;
    otherChannel.channel = 1;
    (void)state.observe(otherChannel, 100000, 5502);
    state.begin(&lane, 0);
    (void)state.observe(key, 100000, 5752);
    state.finish(&lane, 0);
    passed &= require(state.observe(otherChannel, 100000, 5752).overdue,
                      "gather on another channel cannot erase observed stagnation");
    state = {};
    for (std::size_t i = 0; i < policy::kSlots; ++i) {
        auto entry = key;
        entry.token += static_cast<std::uint32_t>(i);
        (void)state.observe(entry, 0, 1000);
    }
    replacement.token += 1000;
    passed &= require(!state.observe(replacement, 0, 1500, {true, 5000}).overdue,
                      "bounded table exhaustion leaves native ordering intact");

    NativeFixture fixture{};
    policy::Key identified{};
    std::uint64_t stamp{};
    constexpr std::uint32_t packed = (1U << 29) | (2U << 27) | 4U;
    passed &= require(movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                         identified, stamp, 1) && stamp == 100,
                      "valid native local pending sobject is identified");
    passed &= require(!movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                          identified, stamp, 0),
                      "another scheduler channel cannot reuse a candidate");
    put(fixture.row, 1, std::uint8_t{0});
    passed &= require(!movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                          identified, stamp, 1),
                      "remote-owned record cannot be reprioritized");
    put(fixture.row, 1, std::uint8_t{0xFF});
    put(fixture.records, 0x3004, std::uint8_t{2});
    passed &= require(!movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                          identified, stamp, 1),
                      "recycled sobject salt cannot pass current identity gate");
    put(fixture.records, 0x3004, std::uint8_t{1});
    put(fixture.auxiliary, 0x18 + 0x10, std::uint32_t{0});
    passed &= require(!movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                          identified, stamp, 1),
                      "clean descriptor cannot be manufactured into pending movement");
    put(fixture.auxiliary, 0x18 + 0x10, std::uint32_t{1});
    put(fixture.auxiliary, 0x18 + 0x38, std::uint16_t{0x83});
    passed &= require(!movement::identify(fixture.row.data(), fixture.lane.data(), packed,
                                          identified, stamp, 1),
                      "creation descriptor remains native");
    std::uint32_t output = packed;
    passed &= require(movement::replace_priority(&output, packed)
                          && (output & ~policy::kPriorityMask) == packed,
                      "priority write preserves token, channel and operation bits");
    passed &= require(!movement::replace_priority(&output, packed),
                      "changed candidate is not overwritten with stale expectation");
    passed &= require((output & policy::kPriorityMask) == policy::kMovementPriority,
                      "movement boost reserves a higher priority for native hit feedback");
    const auto alreadyUrgent = packed | policy::kPriorityMask;
    passed &= require(policy::promote_movement(alreadyUrgent) == alreadyUrgent,
                      "native maximum priority is never demoted");

    EventFixture event{};
    constexpr auto eventPacket = EventFixture::packet;
    passed &= require(feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp) && stamp == 100,
                      "feedback type is resolved from native dispatch rather than selector guess");
    for (const std::uintptr_t method : {0xB7D610U, 0xB7D690U, 0xB7D710U}) {
        put(event.vtable, 0x70, EventFixture::image + method);
        passed &= require(feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp),
                          "all three native damage feedback classes are recognized");
    }
    put(event.head, 0x54, std::uint32_t{1U << 7});
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp),
                      "in-flight feedback is not made eligible for retransmission");
    put(event.head, 0x54, std::uint32_t{0});
    put(event.head, 0x50, std::uint32_t{1U << 6});
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp),
                      "another recipient cannot borrow feedback priority");
    put(event.head, 0x50, std::uint32_t{1U << 7});
    put(event.head, 0x60, std::uint32_t{6});
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp),
                      "recycled event lookup fails the candidate identity check");
    put(event.head, 0x60, std::uint32_t{5});
    passed &= require(!feedback::identify(event.lane.data(), 0, 3, eventPacket, stamp)
                          && !feedback::identify(event.lane.data(), 1, 2, eventPacket, stamp),
                      "channel and lane remain native");
    put(event.head, 4, std::uint32_t{10});
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, eventPacket, stamp),
                      "unrelated native events are not promoted");
    put(event.head, 8, std::int32_t{4});
    put(event.head, 0x58, event.child.data());
    put(event.child, 8, std::int32_t{4});
    put(event.child, 4, std::uint32_t{13});
    put(event.child, 0x50, std::uint32_t{1U << 7});
    put(event.child, 0x10, std::uint64_t{101});
    const auto grouped = eventPacket | (1U << 25);
    passed &= require(feedback::identify(event.lane.data(), 1, 3, grouped, stamp) && stamp == 101,
                      "FIFO group containing feedback retains its native head candidate");
    std::size_t remaining = 1;
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, grouped, stamp, &remaining)
                          && remaining == 0,
                      "total inspection budget bounds grouped queue work");
    put(event.child, 8, std::int32_t{9});
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, grouped, stamp),
                      "feedback from a different FIFO group cannot promote the head");
    put(event.child, 0x58, event.child.data());
    passed &= require(!feedback::identify(event.lane.data(), 1, 3, grouped, stamp),
                      "corrupt cyclic lists fail closed");

    std::vector<std::uint32_t> before, after;
    for (std::uint32_t i = 0; i < 300; ++i) {
        before.push_back((2U << 27) | i | policy::kPriorityMask);
        after.push_back(policy::promote_movement((2U << 27) | i));
    }
    before.push_back(eventPacket | (100U << 15));
    auto promoted = before.back();
    passed &= require(feedback::promote(&promoted, before.back())
                          && (promoted & ~policy::kPriorityMask) == eventPacket,
                      "feedback promotion changes only scheduling priority");
    after.push_back(promoted);
    const auto nativeCompare = [](auto a, auto b) {
        return (a & policy::kPriorityMask) > (b & policy::kPriorityMask);
    };
    std::sort(before.begin(), before.end(), nativeCompare);
    std::sort(after.begin(), after.end(), nativeCompare);
    passed &= require((before[256] & ~policy::kPriorityMask) != eventPacket
                          && (before.back() & ~policy::kPriorityMask) == eventPacket
                          && (after.front() & ~policy::kPriorityMask) == eventPacket,
                      "packet overload regression: feedback excluded before, selected first after");
    EventFixture combat{};
    for (const std::uintptr_t method : {0xEF60A0U, 0xB7D790U, 0xB7D810U, 0xB7DAD0U,
                                        0x1723750U, 0x16E6AA0U}) {
        put(combat.vtable, 0x70, EventFixture::image + method);
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            auto candidate = (eventPacket & ~(3U << 29)) | (channel << 29);
            const auto identity = candidate;
            const auto result = feedback::prioritize(combat.lane.data(), channel, 3, 1, 1, &candidate, {true, 101});
            passed &= require(result.changed == 1 && (candidate & policy::kPriorityMask) == policy::kPriorityMask
                && (candidate & ~policy::kPriorityMask) == identity,
                "strike, fire, throw and hit/death events get prompt priority on both channels");
        }
    }
    put(combat.vtable, 0x70, EventFixture::image + 0xEF60A0);
    auto candidate = eventPacket;
    put(combat.head, 0x18, std::int32_t{1500});
    passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 1599}).changed == 1,
                      "unexpired melee remains eligible at TTL minus one");
    candidate = eventPacket;
    passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 1600}).changed == 0
        && feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {}).changed == 0
        && feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 99}).changed == 0
        && candidate == eventPacket, "expired, unknown-clock and future melee stay native");
    put(combat.head, 0x18, std::int32_t{-1});
    put(combat.head, 0x54, std::uint32_t{1U << 7});
    passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 200}).changed == 0,
                      "melee promotion cannot duplicate an in-flight strike");
    put(combat.head, 0x54, std::uint32_t{0});
    put(combat.lane, 9, std::uint8_t{1});
    passed &= require(!feedback::identify(combat.lane.data(), 1, 3, candidate, stamp), "failed lane stays native");
    put(combat.lane, 9, std::uint8_t{0});
    const auto outsideTable = (eventPacket & ~0x7FFFU) | 2047U;
    passed &= require(!feedback::identify(combat.lane.data(), 1, 3, outsideTable, stamp), "event slot cannot index past pointer table");

    for (const bool sobject : {false, true}) {
        put(combat.vtable, 0x68, sobject ? EventFixture::image + 0x1723570 : std::uintptr_t{0});
        put(combat.vtable, 0x70, sobject ? std::uintptr_t{0} : EventFixture::image + 0x17238C0);
        candidate = eventPacket;
        passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 200}).changed == 0,
                          "ungrouped generic dependency remains native");
        put(combat.head, 8, std::int32_t{33});
        candidate = grouped;
        passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 149}).changed == 0,
                          "grouped dependency gets one normal send interval");
        const auto result = feedback::prioritize(combat.lane.data(), 1, 3, 1, 1, &candidate, {true, 150});
        passed &= require(result.changed == 1 && result.grouped == 1
            && (candidate & ~policy::kPriorityMask) == grouped,
            "aged sobject/reload group promotes only its existing FIFO candidate");
        put(combat.head, 8, std::int32_t{-1});
    }

    put(combat.vtable, 0x68, std::uintptr_t{0});
    put(combat.vtable, 0x70, EventFixture::image + 0xEF60A0);
    std::array<std::array<std::byte, 0x164>, 10> burstNodes{};
    std::array<std::uint32_t, 10> burstPackets{};
    for (std::size_t i = 0; i < burstNodes.size(); ++i) {
        burstNodes[i] = combat.head;
        const auto slot = static_cast<std::uint32_t>(20 + i);
        put(burstNodes[i], 0x60, slot);
        put(burstNodes[i], 0x10, std::uint64_t{190 - i});
        put(combat.manager, 0x9D0 + slot * 8, burstNodes[i].data());
        burstPackets[i] = (eventPacket & ~0x7FFFU) | slot;
    }
    burstPackets[0] |= policy::kPriorityMask;
    const auto savedNodes = burstNodes;
    const auto result = feedback::prioritize(combat.lane.data(), 1, 3, 10, 10, burstPackets.data(), {true, 200});
    passed &= require(result.changed == 8 && result.melee == 10 && result.meleeOldest == 19
        && (burstPackets[1] & policy::kPriorityMask) == 0
        && (burstPackets[9] & policy::kPriorityMask) == policy::kPriorityMask
        && burstNodes == savedNodes,
        "bounded combat burst chooses oldest eight without changing events or consuming slots for already-urgent events");
    for (std::size_t i = 0; i < burstPackets.size(); ++i)
        passed &= require((burstPackets[i] & ~policy::kPriorityMask) == ((eventPacket & ~0x7FFFU) | (20U + i)),
                          "burst selection retains every original event identity and position");
    const auto savedPackets = burstPackets;
    passed &= require(feedback::prioritize(combat.lane.data(), 1, 3, 9, 10, burstPackets.data(), {true, 200}).changed == 0
        && burstPackets == savedPackets, "bad native capacity cannot mutate output");
    // live capture: native incident notifications waited nearly their whole 5000 ms lifetime
    // below the movement floor
    EventFixture incident{};
    put(incident.vtable, 0x70, EventFixture::image + 0x16E6C80);
    put(incident.head, 0x18, std::int32_t{5000});
    candidate = eventPacket | (613U << 15);
    const auto incidentOriginal = candidate;
    passed &= require(feedback::prioritize(incident.lane.data(), 1, 3, 1, 1, &candidate, {true,149}).changed == 0
        && candidate == incidentOriginal, "young incident retains native priority");
    passed &= require(feedback::prioritize(incident.lane.data(), 1, 3, 1, 1, &candidate, {true,150}).changed == 1
        && (candidate & policy::kPriorityMask) == policy::kPriorityMask
        && (candidate & ~policy::kPriorityMask) == eventPacket,
        "incident reaches send priority at 50 ms without changing identity");
    candidate = incidentOriginal;
    passed &= require(feedback::prioritize(incident.lane.data(), 1, 3, 1, 1, &candidate, {true,5100}).changed == 0
        && candidate == incidentOriginal, "expired incident is never revived");
    put(incident.head, 0x54, std::uint32_t{1U<<7});
    passed &= require(feedback::prioritize(incident.lane.data(), 1, 3, 1, 1, &candidate, {true,200}).changed == 0,
        "incident awaiting ACK is not duplicated");
    put(incident.head, 0x54, std::uint32_t{0});
    put(incident.registry, 0x30 + 14 * 8, combat.definition.data());
    for (std::size_t i=0;i<burstNodes.size();++i) {
        burstNodes[i]=incident.head;
        put(burstNodes[i],0x60,static_cast<std::uint32_t>(20+i));
        put(burstNodes[i],0x10,std::uint64_t{100+i});
        put(incident.manager,0x9D0+(20+i)*8,burstNodes[i].data());
        burstPackets[i]=(eventPacket&~0x7FFFU)|static_cast<std::uint32_t>(20+i);
    }
    const auto incidentNodes=burstNodes;
    passed &= require(feedback::prioritize(incident.lane.data(),1,3,10,10,burstPackets.data(),{true,200}).changed==2
        && (burstPackets[0]&policy::kPriorityMask)==policy::kPriorityMask
        && (burstPackets[1]&policy::kPriorityMask)==policy::kPriorityMask
        && (burstPackets[2]&policy::kPriorityMask)==0 && burstNodes==incidentNodes,
        "incident burst chooses oldest two and leaves payloads untouched");
    for (std::size_t i=0;i<burstNodes.size();++i) {
        burstPackets[i]&=~policy::kPriorityMask;
        if(i>=2) { put(burstNodes[i],4,std::uint32_t{14}); put(burstNodes[i],0x10,std::uint64_t{190}); }
    }
    passed &= require(feedback::prioritize(incident.lane.data(),1,3,10,10,burstPackets.data(),{true,200}).changed==8
        && (burstPackets[0]&policy::kPriorityMask)==0 && (burstPackets[1]&policy::kPriorityMask)==0
        && (burstPackets[9]&policy::kPriorityMask)==policy::kPriorityMask,
        "older incidents cannot displace eight immediate combat events");
    put(incident.head,4,std::uint32_t{10});
    put(incident.head,8,std::int32_t{33});
    put(incident.head,0x58,incident.child.data());
    put(incident.child,4,std::uint32_t{13});
    put(incident.child,8,std::int32_t{33});
    put(incident.child,0x10,std::uint64_t{100});
    put(incident.child,0x18,std::int32_t{5000});
    put(incident.child,0x50,std::uint32_t{1U<<7});
    candidate=grouped;
    passed &= require(feedback::prioritize(incident.lane.data(),1,3,1,1,&candidate,{true,150}).changed==1
        && (candidate&~policy::kPriorityMask)==grouped, "incident dependency FIFO head is preserved");
    burstNodes[0]=incident.child;
    put(burstNodes[0],4,std::uint32_t{14});
    put(incident.child,0x58,burstNodes[0].data());
    feedback::Kind groupKind{};
    passed &= require(feedback::identify(incident.lane.data(),1,3,grouped,stamp,nullptr,&groupKind,{true,150})
        && groupKind==feedback::Kind::melee,
        "incident ahead of melee in one FIFO retains immediate combat classification");
    feedback::imageBase.store(0);
    movement::clockRoot.store(nullptr);
    movement::sobjectPool.store(nullptr);
    if (passed) {
        std::puts("entity movement fairness: PASS");
    }
    return passed ? 0 : 1;
}
