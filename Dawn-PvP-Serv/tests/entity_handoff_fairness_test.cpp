#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include "client/hooks/bootflow/entity_handoff_fairness.h"

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace hand = dawn::client::hooks::bootflow::entity_handoff_fairness;
namespace life = dawn::client::hooks::bootflow::entity_lifecycle_fairness;
namespace mem = dawn::client::hooks::bootflow::entity_movement_fairness;
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
    static constexpr std::uint32_t packed = 2U << 27;
    std::array<std::byte,0xD000> manager{};
    std::array<std::byte,0x70 * 8> rows{};
    std::array<std::array<std::byte,0x98>,8> aux{};
    std::array<std::byte,0x40> lane{};
    Fixture() {
        life::imageBase = image; mem::table = rows.data(); hand::configured = true; hand::scanCursor = 0;
        put(lane.data(), 0, image + 0x1C9ADD8);
        put(lane.data(), 9, std::uint8_t{1}); put(lane.data(), 10, std::uint8_t{1});
        put(lane.data(), 0x10, manager.data()); put(manager.data(), 0xC520, std::uint32_t{255});
        for (std::uint32_t i = 0; i < 8; ++i) {
            put(manager.data(), 0x114 + i * 6, static_cast<std::int16_t>(i));
            put(row(i), 1, std::uint8_t{0xFD}); put(row(i), 8, 0x10000U + i);
            put(row(i), 0xC, UINT32_MAX); put(row(i), 0x60, aux[i].data());
            put(aux[i].data(), 4, std::uint32_t{1});
            put(aux[i].data(), 0x38, std::uint64_t{100}); flags(i, 0x68A);
        }
    }
    std::byte* row(std::size_t i) { return rows.data() + i * 0x70; }
    void flags(std::size_t i, std::uint16_t value) { put(aux[i].data(), 0x50, value); }
    std::uint64_t age(std::uint64_t now = 350, bool valid = true, std::uint32_t channel = 0) {
        return hand::eligible_age(lane.data(), packed | (channel << 29), {valid,now}, channel);
    }
};
int main(int argc, char** argv) {
    Fixture f; bool ok = true;
    auto output = Fixture::packed | (204U << 15);
    ok &= check(f.age() == 250, "captured outgoing handoff with native eligible flags accepted");
    ok &= check(!hand::apply(f.lane.data(),0,2,1,1,&output,{true,349}), "young handoffs unchanged");
    ok &= check(!f.age(99) && !f.age(500,false), "unknown/future clock rejected");
    for (std::uint32_t ch = 0; ch < 3; ++ch) ok &= check(f.age(350,true,ch) == 250, "all exact native channels supported");
    ok &= check(!f.age(350,true,3), "invalid channel rejected");
    for (const auto owner : {0U, 1U, 0xFFU, 0xFEU}) {
        put(f.row(0),1,static_cast<std::uint8_t>(owner));
        ok &= check(!f.age(), "only outgoing-transfer owner qualifies");
    }
    put(f.row(0),1,std::uint8_t{0xFD});
    for (const auto flags : {0x8U,0x82U,0x689U,0x68CU,0x68DU}) {
        f.flags(0,static_cast<std::uint16_t>(flags));
        ok &= check(!f.age(), "unadmitted/snapshot/creation/removal work rejected");
    }
    f.flags(0,0x688); ok &= check(f.age() == 250, "handoff without snapshot dirty bit qualifies");
    put(f.aux[0].data(),0,std::int8_t{1}); ok &= check(!f.age(), "wrong transfer destination rejected");
    put(f.aux[0].data(),0,std::int8_t{0});
    put(f.aux[0].data(),4,std::uint32_t{2}); ok &= check(!f.age(), "unregistered recipient rejected");
    put(f.aux[0].data(),4,std::uint32_t{1});
    put(f.manager.data(),0xC520,std::uint32_t{254}); ok &= check(!f.age(), "unallocated token rejected");
    put(f.manager.data(),0xC520,std::uint32_t{255});
    put(f.row(0),8,std::uint32_t{0x10001}); ok &= check(!f.age(), "recycled row mapping rejected");
    put(f.row(0),8,std::uint32_t{0x10000});
    put(f.row(0),0xC,std::uint32_t{0x10001}); ok &= check(!f.age(), "dependent child is not independently promoted");
    put(f.row(0),0xC,UINT32_MAX);
    for (const auto flags : {4U,0x200U}) {
        put(f.row(0),0x50,static_cast<std::uint16_t>(flags)); ok &= check(!f.age(), "retiring/blocked root rejected");
    }
    put(f.row(0),0x50,std::uint16_t{0});
    const auto savedRows = f.rows; const auto savedAux = f.aux; const auto savedManager = f.manager;
    std::array<std::uint32_t,2> queue{output,Fixture::packed | 1U | policy::kMovementPriority};
    put(f.row(1),1,std::uint8_t{0xFF}); f.flags(1,0x82);
    const auto highest = [&]() { return std::max_element(queue.begin(),queue.end(),[](auto a,auto b) {
        return (a & policy::kPriorityMask) < (b & policy::kPriorityMask);
    }) - queue.begin(); };
    ok &= check(highest() == 1, "old policy always selects movement before pending handoff");
    const auto beforeRows = f.rows; const auto beforeAux = f.aux;
    ok &= check(hand::apply(f.lane.data(),0,2,2,2,queue.data(),{true,500}) == 1 && highest() == 0
        && queue[1] == (Fixture::packed | 1U | policy::kMovementPriority)
        && f.rows == beforeRows && f.aux == beforeAux && f.manager == savedManager,
        "handoff progresses without lowering movement or editing ownership/ACK/data");
    f.rows = savedRows; f.aux = savedAux;
    std::array<std::uint32_t,8> batch{};
    for (std::uint32_t i = 0; i < 8; ++i) {
        batch[i] = Fixture::packed | i | (204U << 15);
        put(f.aux[i].data(),0x38,std::uint64_t{100 + i});
    }
    ok &= check(hand::apply(f.lane.data(),0,2,8,8,batch.data(),{true,500}) == 2
        && (batch[0] & policy::kPriorityMask) == policy::kPriorityMask
        && (batch[1] & policy::kPriorityMask) == policy::kPriorityMask
        && (batch[2] & policy::kPriorityMask) == (204U << 15), "bounded oldest-two handoff burst");
    const auto unchanged = batch;
    ok &= check(!hand::apply(f.lane.data(),1,2,8,8,batch.data(),{true,500})
        && !hand::apply(f.lane.data(),0,1,8,8,batch.data(),{true,500})
        && !hand::apply(f.lane.data(),0,2,1,8,batch.data(),{true,500})
        && !hand::apply(f.lane.data(),0,2,8,-1,batch.data(),{true,500}) && batch == unchanged,
        "malformed count/channel/lane rejected");
    hand::configured = false;
    ok &= check(!hand::apply(f.lane.data(),0,2,8,8,batch.data(),{true,500}), "disabled policy is passthrough");
    if (argc == 2) {
        auto* image = static_cast<std::byte*>(VirtualAlloc(nullptr,0x1900000,MEM_RESERVE | MEM_COMMIT,PAGE_READWRITE));
        if (!image) return 1;
        std::ifstream input(argv[1],std::ios::binary);
        for (const auto at : {0x1714052U,0x171D672U,0x170C1A0U}) {
            input.seekg(at - 0x1000U); input.read(reinterpret_cast<char*>(image + at),16);
            if (!input.good()) return 1;
        }
        life::imageBase = reinterpret_cast<std::uintptr_t>(image);
        ok &= check(hand::configure(life::imageBase), "guards match captured native producer/owner/gather");
        for (const auto at : {0x1714052U,0x171D672U,0x170C1A0U}) {
            image[at] ^= std::byte{1};
            ok &= check(!hand::configure(life::imageBase), "changed native contract disables policy");
            image[at] ^= std::byte{1};
        }
        VirtualFree(image,0,MEM_RELEASE);
    } else if (argc != 1) return 2;
    if (ok) std::puts("entity handoff fairness: PASS");
    return ok ? 0 : 1;
}
