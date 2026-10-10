#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include "../dawn/src/client/hooks/teleport/teleport_move.cpp"
#include "../dawn/src/client/player/player_position.cpp"
namespace f {
unsigned checks{};
void require(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1); }
}
std::uint32_t* controlled(std::uint32_t* out) { *out=1;return out; }
std::uint32_t* controlled_zero(std::uint32_t* out) { *out=0;return out; }
std::uint32_t* physics(std::uint32_t* out, std::uint32_t) { *out=1;return out; }
std::uint32_t* faulting(std::uint32_t*) { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return nullptr; }
struct BodyFixture {
    std::array<std::byte,0x240> component{};
    std::array<std::byte,80> rows{};
    std::array<std::byte,0x240> body{};
    explicit BodyFixture(float value) {
        std::uint32_t type=0x80808A0CU,owner=1;std::byte* array=rows.data();std::byte* rigid=body.data();
        const dawn::client::hooks::teleport::Vector position{value,value*2,value*3};
        std::memcpy(component.data()+44,&owner,sizeof owner);
        std::memcpy(component.data()+4,&type,sizeof type);
        std::memcpy(component.data()+400,&array,sizeof array);
        std::memcpy(rows.data()+32,&rigid,sizeof rigid);
        std::memcpy(body.data()+448,&position,sizeof position);
    }
};
}
int main() {
    namespace t=dawn::client::hooks::teleport;
    namespace p=dawn::client::player::position;
    f::BodyFixture a(1),b(4),c(7),d(10);
    f::BodyFixture unused(20);
    const std::uint32_t absent=0xFFFFFFFFU,zero=0;
    std::memcpy(unused.component.data()+4,&absent,sizeof absent);
    std::memcpy(unused.component.data()+44,&zero,sizeof zero);
    t::publish_targets(&f::controlled_zero);
    f::require(t::owns_local_player(unused.component.data()),"live unused owner zero reproduces legacy low-index match");
    p::observe(unused.component.data());
    f::require(!p::snapshot().present && p::g_component.load()==nullptr,
        "FFFFFFFF class/owner zero cannot publish or occupy position cache even with readable body");
    const std::uint32_t correctType=0x80808A0CU,wrongGeneration=0x10001;
    std::memcpy(unused.component.data()+4,&correctType,sizeof correctType);
    std::memcpy(unused.component.data()+44,&wrongGeneration,sizeof wrongGeneration);
    t::publish_targets(&f::controlled);
    f::require(t::owns_local_player(unused.component.data()),"reused datum generation reproduces legacy low-index match");
    p::observe(unused.component.data());
    f::require(!p::snapshot().present && p::g_component.load()==nullptr,
        "typed component with different full owner generation cannot occupy position cache");
    t::publish_targets(&f::controlled);
    f::require(t::owns_local_player(a.component.data()),"original controlled ownership remains available");
    p::observe(a.component.data());
    f::require(p::snapshot().present && p::snapshot().position==t::Vector{1,2,3},"physics observation publishes world position");
    std::array<std::byte,0x480> nativeRows{};
    std::memcpy(nativeRows.data()+0x240,b.component.data(),b.component.size());
    std::array<std::byte,0x40> descriptor{};
    auto* rowBase=nativeRows.data();
    const std::uint32_t stride=0x240,unitStride=0xE0,capacity=4096,owner=1;
    std::memcpy(descriptor.data()+8,&rowBase,sizeof rowBase);
    std::memcpy(descriptor.data()+0x30,&stride,sizeof stride);
    auto* table=descriptor.data();auto** holder=&table;
    std::vector<std::byte> units(static_cast<std::size_t>(capacity)*unitStride);
    auto* unitBase=units.data();
    std::memcpy(units.data()+unitStride+0xC,&owner,sizeof owner);
    t::publish_native_player_lookup({&f::physics,reinterpret_cast<const std::byte*>(&holder),
        reinterpret_cast<const std::byte*>(&unitBase),reinterpret_cast<const std::byte*>(&unitStride),
        reinterpret_cast<const std::byte*>(&capacity)});
    std::memcpy(a.component.data()+4,&absent,sizeof absent);
    p::poll();
    f::require(p::g_component.load()==nativeRows.data()+0x240 && p::snapshot().position==t::Vector{4,8,12},
        "retired cached class is dropped and native body recovered in the same poll");
    std::memcpy(a.component.data()+4,&correctType,sizeof correctType);
    t::publish_native_player_lookup({});
    p::reset();
    const std::int32_t noBody=65536;
    std::memcpy(unused.component.data()+44,&owner,sizeof owner);
    std::memcpy(unused.component.data()+516,&noBody,sizeof noBody);
    p::observe(unused.component.data());
    f::require(!p::snapshot().present && p::g_component.load()==nullptr,
        "owned typed component without a usable body cannot occupy the cache");
    p::observe(a.component.data());
    t::publish_targets(&f::faulting);
    f::require(!t::owns_local_player(a.component.data()),"native getter retirement faults return missing ownership");
    f::require(t::local_player_component()==nullptr,"native body miss has no fabricated fallback component");
    p::poll();f::require(!p::snapshot().present,"invalid ownership clears HUD presence");
    t::publish_targets(&f::controlled);
    std::atomic_bool done{},bad{};
    std::thread reader([&] {
        while (!done.load()) {
            const auto snapshot=p::snapshot();
            if (snapshot.present && (snapshot.position[1]!=snapshot.position[0]*2
                || snapshot.position[2]!=snapshot.position[0]*3)) bad.store(true);
        }
    });
    std::vector<std::thread> writers;
    for (auto* body:{&a,&b,&c,&d}) writers.emplace_back([&,body] {
        for (unsigned i=0;i<10000;++i) { p::reset();p::observe(body->component.data());p::poll(); }
    });
    for (auto& writer:writers) writer.join();done.store(true);reader.join();
    f::require(!bad.load(),"four observer writers and reader cannot mix vector lanes");
    const std::int32_t invalid=65536;std::memcpy(a.component.data()+516,&invalid,sizeof invalid);
    t::Vector ignored{};f::require(!t::read_position(a.component.data(),ignored),"body array upper bound is rejected");
    t::clear_targets();p::reset();
    f::require(!t::owns_local_player(b.component.data())&&!p::snapshot().present,"teardown releases helpers and snapshot");
    std::printf("PASS %u player observer lifetime/snapshot checks; 40000 multiwriter cycles; no game access\n",f::checks);
}
