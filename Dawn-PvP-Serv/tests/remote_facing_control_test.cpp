#include "../dawn/src/client/hooks/bootflow/remote_facing_control.h"
#include <limits>

namespace control = dawn::client::hooks::bootflow::remote_facing_control;
namespace policy = dawn::client::hooks::bootflow::remote_facing_policy;
namespace detour = dawn::client::hooking::detour;
static detour::UninstallResult detachResult = detour::UninstallResult::failed;
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec>,std::span<Handle>) noexcept { return false; }
UninstallResult uninstall(std::span<Handle> handles,std::span<const ProtectedCodeEntry>,IdleCheck check) noexcept {
    if (check && !check()) return UninstallResult::protectedCodeActive;
    if (detachResult==UninstallResult::removed) for (auto& handle:handles) handle={};
    return detachResult;
}
}

static unsigned failures{}, applyCalls{}, passCalls{}, directionCalls{};
static void check(bool value,const char* label) {
    if (!value) { std::printf("FAIL: %s\n",label); ++failures; }
}
template<class T> static void put(void* base,std::size_t offset,T value) {
    std::memcpy(static_cast<std::byte*>(base)+offset,&value,sizeof(value));
}
static void __fastcall native_apply(void*,const void*,void*) noexcept { ++applyCalls; SetLastError(123); }
static void __fastcall native_pass() noexcept { ++passCalls; SetLastError(456); }
static void __fastcall native_direction(void* component,void*) noexcept {
    ++directionCalls;
    auto* bytes=static_cast<std::byte*>(component);
    std::memcpy(bytes+0xD0,bytes+0x420,sizeof(policy::Vector));
    std::memcpy(bytes+0xE0,bytes+0x420,sizeof(policy::Vector));
}
int main() {
    constexpr std::size_t imageSize=0x26BE0E0+1024;
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* rows=static_cast<std::byte*>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!image || !rows) { std::puts("FAIL: synthetic memory allocation"); return 1; }
    alignas(16) std::array<std::byte,0x80> descriptors{};
    const auto* table=descriptors.data(); const auto* root=&table;
    std::array<std::uint32_t,256> suppression{};
    auto* component=rows+0x1000;
    put(image,0x2439C70,root);
    put(image,0x1FBA9C8,suppression.data());
    put(descriptors.data(),0x40+8,rows);
    put(descriptors.data(),0x40+0x30,std::uint32_t{0x1000});
    put(descriptors.data(),0x40+0x34,std::int32_t{0});
    const policy::Vector previous{1,0,0,0}, received{0,1,0,0};
    const auto prepare=[&] {
        std::memset(component,0,0x1000);
        put(component,0,std::uint32_t{0x80C0C51A}); put(component,4,std::uint32_t{0x80803A00});
        put(component,0x24,std::uint32_t{0x2001}); put(component,0x2C,std::uint32_t{0x10001});
        put(component,0xACE,std::uint8_t{1}); put(component,0x410,std::uint8_t{2});
        put(component,0x8A0,UINT32_MAX);
        put(component,0x420,received); put(component,0xD0,previous); put(component,0xE0,previous);
        put(component,0xB14,std::array<float,3>{1,0,0}); put(component,0xB38,std::array<float,3>{1,0,0});
        put(image,0x26BE0E0,std::uint32_t{0}); suppression={};
        control::pending={}; control::publications.store(0);
        control::imageBase.store(reinterpret_cast<std::uintptr_t>(image));
        control::originalApply.store(&native_apply); control::originalPass.store(&native_pass);
        control::nativeDirection.store(&native_direction);
        applyCalls=passCalls=directionCalls=0;
    };
    prepare();
    std::array<std::byte,0x1000> before{}; std::memcpy(before.data(),component,before.size());
    control::apply(component,nullptr,nullptr);
    check(applyCalls==1 && directionCalls==0 && GetLastError()==123,
          "network apply calls original and queues only identity, preserving last error");
    control::control_pass();
    check(passCalls==1 && directionCalls==1 && control::publications.load()==1 && GetLastError()==456,
          "one normal control pass performs one supported facing publication");
    for (std::size_t i=0;i<before.size();++i) {
        const bool allowed=(i>=0xD0 && i<0xF0) || (i>=0xB14 && i<0xB20) || (i>=0xB38 && i<0xB44);
        if (!allowed && component[i]!=before[i]) { check(false,"unrelated position, velocity, timestamp and state bytes unchanged"); break; }
    }
    policy::Vector actual{};
    std::memcpy(&actual,component+0xD0,sizeof(actual)); check(!policy::different(actual,received),"control takes received direction");
    std::array<float,3> facing{}; std::memcpy(&facing,component+0xB14,sizeof(facing));
    check(facing==std::array<float,3>{0,1,0},"native facing fields receive exact direction without extrapolation");
    std::array<std::byte,0x1000> fixed{}; std::memcpy(fixed.data(),component,fixed.size());
    for (unsigned i=0;i<100;++i) { control::apply(component,nullptr,nullptr); control::control_pass(); }
    check(directionCalls==1 && std::memcmp(fixed.data(),component,fixed.size())==0,
          "repeated identical updates make no extra publication and cause no drift");

    prepare(); control::apply(component,nullptr,nullptr);
    AcquireSRWLockExclusive(&control::controlLock);
    control::control_pass();
    ReleaseSRWLockExclusive(&control::controlLock);
    check(directionCalls==0 && control::pending.entries[0].key.component,
          "busy control pass retains queued work without concurrent mutation");
    control::control_pass();
    check(directionCalls==1,"next idle control pass consumes the retained work once");

    const auto refused=[&](auto change,const char* label) {
        prepare(); control::apply(component,nullptr,nullptr); change();
        std::array<std::byte,0x1000> original{}; std::memcpy(original.data(),component,original.size());
        control::control_pass();
        check(directionCalls==0 && std::memcmp(original.data(),component,original.size())==0,label);
    };
    refused([&] { put(image,0x26BE0E0,std::uint32_t{2}); },"local authority acquired after receive refuses queued work");
    refused([&] { suppression[0]=2; },"suppression acquired after receive refuses queued work");
    refused([&] { put(component,0x2C,std::uint32_t{0x20001}); },"legacy generation reuse with same index refuses queued work");
    refused([&] { put(component,0x24,std::uint32_t{0x4001}); },"typed owner reuse refuses queued work");
    refused([&] { put(component,0x410,std::uint8_t{3}); },"new control incarnation refuses queued work");
    refused([&] { put(component,0x8A0,std::uint32_t{0x3001}); },"mounted controller refuses queued work");
    refused([&] { put(component,0xB0,std::uint32_t{1}); },"control flags refuse queued work");
    refused([&] { put(component,0xBF8,std::uint8_t{1}); },"transition refuses queued work");
    refused([&] { put(component,0x420,policy::Vector{std::numeric_limits<float>::quiet_NaN(),1,0,0}); },"invalid vector refuses queued work");
    refused([&] { for (auto& e:control::pending.entries) if (e.key.component) e.stamp=GetTickCount64()+1001; },
            "backwards clock refuses queued work");

    prepare(); policy::Key key{}; policy::State state{};
    check(control::identify(component,key,state),"synthetic typed component resolves");
    const policy::Pending input{key,1000};
    check(policy::applies(input,key,state,2000) && !policy::applies(input,key,state,2001)
          && !policy::applies(input,key,state,999),"queue age is bounded to one second");
    policy::Queue bounded{};
    for (std::size_t i=0;i<bounded.capacity;++i) { auto next=key; next.component+=i*16; check(bounded.push(next,1000),"queue fills fixed capacity"); }
    auto extra=key; extra.component+=bounded.capacity*16;
    check(!bounded.push(extra,1000) && bounded.push(extra,2001),"full queue refuses active eviction and reuses expired entries");

    control::handles={{{reinterpret_cast<void*>(&native_apply),reinterpret_cast<void*>(&control::apply),true},
                        {reinterpret_cast<void*>(&native_pass),reinterpret_cast<void*>(&control::control_pass),true}}};
    detachResult=detour::UninstallResult::failed;
    check(!control::uninstall(),"detach failure propagates to the runtime owner");
    check(control::imageBase.load()==0 && control::handles[0].attached && control::originalApply.load()==&native_apply,
          "failed detach disables mutation and retains callable original");
    const auto callsBefore=applyCalls;
    control::apply(component,nullptr,nullptr);
    check(applyCalls==callsBefore+1 && directionCalls==0,"retained hook passes through after detach refusal");
    detachResult=detour::UninstallResult::removed; control::activeCalls.store(1);
    check(!control::uninstall(),"active callback refusal propagates to the runtime owner");
    check(control::handles[0].attached && control::originalPass.load()==&native_pass,
          "active native callback prevents trampoline reclamation");
    control::activeCalls.store(0); check(control::uninstall(),"idle detach succeeds");
    check(!control::handles[0].attached && !control::originalApply.load() && !control::originalPass.load(),
          "successful idle detach clears hooks and queue");
    VirtualFree(rows,0,MEM_RELEASE); VirtualFree(image,0,MEM_RELEASE);
    if (!failures) std::puts("PASS: remote facing phase, exact writes, identity/authority/epoch guards, queue lifetime, idempotence and detach failure");
    return failures ? 1 : 0;
}
