#include <cassert>
#include <string>
#include <vector>
#include "../dawn/src/client/hooks/queuez/svc123_null_payload_guard.cpp"

namespace {
std::vector<std::string> events;
unsigned originalCalls{};
char __fastcall original(void*, void*, std::byte*) { ++originalCalls; return 7; }
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
struct PendingFixture {
    static constexpr std::uintptr_t image = 0x10000000, connection = 0x50000000;
    static constexpr std::uintptr_t pool = 0x50001000, table = 0x50002000, resource = 0x50003000;
    static constexpr std::uintptr_t header = 0x50004000, elements = 0x50005008;
    static constexpr std::uintptr_t object = 0x50006000, vtable = 0x50007000;
    static constexpr std::uintptr_t descriptor = 0x50008000, status = 0x50009000;
    static constexpr std::uintptr_t nestedObject = 0x5000A000, nestedVtable = 0x5000B000;
    struct Region { std::uintptr_t base; std::vector<std::byte> bytes; };
    std::vector<Region> regions;
    std::uintptr_t failAddress{}, changeAddress{};
    unsigned changedReads{}, calls{};
    bool executable{true};
    std::size_t totalBytes{};
    PendingFixture() {
        regions = {{image + 0x352310, std::vector<std::byte>(12)},
            {image + 0xE0249D, std::vector<std::byte>(17)},
            {image + 0x2439C70, std::vector<std::byte>(8)},
            {image + 0xB94700, std::vector<std::byte>(32)},
            {connection, std::vector<std::byte>(0x88)}, {pool, std::vector<std::byte>(24)},
            {table, std::vector<std::byte>(64)}, {resource, std::vector<std::byte>(36)},
            {header, std::vector<std::byte>(0x1E)}, {elements - 8, std::vector<std::byte>(0x80)},
            {object, std::vector<std::byte>(32)}, {vtable, std::vector<std::byte>(8)},
            {descriptor, std::vector<std::byte>(16)}, {status, std::vector<std::byte>(1)}};
        const std::array<unsigned char,12> lookup{0x48,0x83,0xEC,0x08,0x44,0x8B,0x51,0x04,0x4C,0x8B,0xCA,0xC7};
        const std::array<unsigned char,17> completion{0x49,0x8D,0x8D,0x80,0,0,0,0x48,0x8D,0x54,0x24,0x34,0xE8,0x62,0xFE,0x54,0xFF};
        put(image+0x352310, lookup); put(image+0xE0249D, completion);
        put(image+0x2439C70,pool); put(connection+0x80,std::uint32_t{0x12345678});
        put(connection+0x84,std::uint32_t{0}); put(pool,table); put(pool+0x10,std::uint32_t{64});
        put(table+8,elements); put(table+0x10,resource); put(table+0x30,std::uint32_t{32});
        put(table+0x34,std::int32_t{0}); put(resource,header); put(resource+8,elements);
        put(resource+0x1C,std::uint32_t{16}); put(resource+0x20,std::uint32_t{32});
        put(header+0x1C,std::uint16_t{1}); put(elements,object); put(elements+8,std::uintptr_t{0});
        put(elements+16,std::uint32_t{0x12345678}); put(object,vtable); put(vtable,image+0xB94700);
        put(descriptor+8,status); put(status,static_cast<unsigned char>(31));
    }
    void admit_intermediate() {
        regions.push_back({image+0x105BE10,std::vector<std::byte>(32)});
        regions.push_back({image+0x105C06D,std::vector<std::byte>(6)});
        regions.push_back({image+0x105C0C7,std::vector<std::byte>(14)});
        regions.push_back({nestedObject,std::vector<std::byte>(32)});
        regions.push_back({nestedVtable,std::vector<std::byte>(8)});
        const std::array<unsigned char,32> intermediate{
            0x4C,0x8B,0xDC,0x55,0x56,0x49,0x8D,0xAB,0x68,0xFC,0xFF,0xFF,
            0x48,0x81,0xEC,0x88,0x04,0,0,0x48,0x8B,0x05,0x5E,0xDC,0x04,0x01,
            0x48,0x33,0xC4,0x48,0x89,0x85};
        const std::array<unsigned char,6> slot{0x80,0x7E,0x08,0xFF,0x74,0x54};
        const std::array<unsigned char,14> nested{
            0x48,0x8D,0x4E,0x10,0x48,0x8D,0x54,0x24,0x30,0xE8,0x3B,0x62,0x2F,0xFF};
        put(image+0x105BE10,intermediate); put(image+0x105C06D,slot); put(image+0x105C0C7,nested);
        put(vtable,image+0x105BE10); put(object+8,static_cast<unsigned char>(0xFF));
        put(object+0x10,std::uint32_t{0x89ABCDEF}); put(object+0x14,std::uint32_t{1});
        put(header+0x1C,std::uint16_t{2}); put(elements+32,nestedObject);
        put(elements+40,std::uintptr_t{0}); put(elements+48,std::uint32_t{0x89ABCDEF});
        put(nestedObject,nestedVtable); put(nestedVtable,image+0xB94700);
    }
    template<class T> void put(std::uintptr_t address,const T& value) {
        for(auto& r:regions) if(address>=r.base && address-r.base<=r.bytes.size()
            && sizeof value<=r.bytes.size()-(address-r.base)) {
            std::memcpy(r.bytes.data()+(address-r.base),&value,sizeof value); return;
        }
        assert(false);
    }
    bool read(std::uintptr_t address,void* output,std::size_t size) noexcept {
        ++calls; totalBytes+=size; SetLastError(992);
        if(address==failAddress) return false;
        for(const auto& r:regions) if(address>=r.base && address-r.base<=r.bytes.size()
            && size<=r.bytes.size()-(address-r.base)) {
            std::memcpy(output,r.bytes.data()+(address-r.base),size);
            if(address==changeAddress && ++changedReads==2) static_cast<std::byte*>(output)[0]^=std::byte{1};
            return true;
        }
        return false;
    }
    auto sample(std::uintptr_t useConnection=connection) {
        SetLastError(91);
        const auto result=dawn::client::hooks::queuez::pending_owner_trace(image,0x3000000,
            useConnection,descriptor,[&](std::uintptr_t a,void* p,std::size_t n) noexcept {return read(a,p,n);},
            [&](std::uintptr_t a) noexcept {SetLastError(993); return executable
                && (a==image+0xB94700 || a==image+0x105BE10);});
        assert(GetLastError()==91 && calls<=128 && totalBytes<=8192);
        return result;
    }
};
void test_pending_owner_snapshot() {
    PendingFixture good;
    auto result=good.sample();
    assert(result.available && result.statusKnown && result.status==31);
    assert(result.locator==PendingFixture::object && result.callbackRva==0xB94700);
    assert(!result.slotKnown && !result.nestedAvailable && result.nestedCallbackRva==0);
    assert(std::string(result.nestedReason)=="not_selected" && good.calls<=48);
    for(const auto address:{PendingFixture::connection+0x80,PendingFixture::pool,
        PendingFixture::table,PendingFixture::resource,PendingFixture::header+0x1C,
        PendingFixture::elements+16,PendingFixture::object,PendingFixture::vtable,
        PendingFixture::image+0xB94700,PendingFixture::descriptor+8,PendingFixture::status}) {
        PendingFixture changed; changed.changeAddress=address;
        result=changed.sample();
        assert(!result.available && !result.statusKnown && result.locator==0 && result.callbackRva==0);
        assert(std::string(result.reason)=="snapshot_changed");
    }
    for(const auto address:{PendingFixture::image+0x352310,PendingFixture::connection+0x80,
        PendingFixture::pool,PendingFixture::table,PendingFixture::resource,
        PendingFixture::header+0x1C,PendingFixture::elements+16,
        PendingFixture::object,PendingFixture::vtable,PendingFixture::image+0xB94700}) {
        PendingFixture fault; fault.failAddress=address;
        result=fault.sample(); assert(!result.available && result.locator==0 && result.callbackRva==0);
    }
    PendingFixture wrongKey; wrongKey.put(PendingFixture::elements+16,std::uint32_t{1});
    assert(std::string(wrongKey.sample().reason)=="pending_key");
    PendingFixture missing; missing.put(PendingFixture::connection+0x84,UINT32_MAX);
    assert(std::string(missing.sample().reason)=="no_pending_handle");
    PendingFixture index; index.put(PendingFixture::connection+0x84,std::uint32_t{1});
    assert(std::string(index.sample().reason)=="element_bounds");
    PendingFixture stride; stride.put(PendingFixture::resource+0x20,UINT32_MAX);
    assert(std::string(stride.sample().reason)=="resource_layout");
    PendingFixture poolStride; poolStride.put(PendingFixture::pool+0x10,std::uint32_t{32});
    assert(std::string(poolStride.sample().reason)=="pool_layout");
    PendingFixture keyOffset; keyOffset.put(PendingFixture::resource+0x1C,std::uint32_t{31});
    assert(std::string(keyOffset.sample().reason)=="resource_layout");
    PendingFixture count; count.put(PendingFixture::header+0x1C,std::uint16_t{8193});
    assert(std::string(count.sample().reason)=="element_bounds");
    PendingFixture wrongCode; wrongCode.put(PendingFixture::image+0x352310,std::uint32_t{0});
    assert(std::string(wrongCode.sample().reason)=="native_layout");
    PendingFixture overflow; overflow.put(PendingFixture::pool,UINTPTR_MAX-8);
    assert(!overflow.sample().available);
    PendingFixture external; external.put(PendingFixture::vtable,std::uintptr_t{0x70000000});
    assert(std::string(external.sample().reason)=="callback_external");
    PendingFixture nonCode; nonCode.executable=false;
    assert(std::string(nonCode.sample().reason)=="callback_not_code");
    PendingFixture noStatus; noStatus.failAddress=PendingFixture::status;
    result=noStatus.sample(); assert(result.available && !result.statusKnown);
    PendingFixture masked; masked.put(PendingFixture::table+0x34,std::int32_t{8});
    masked.put(PendingFixture::elements+8,std::uintptr_t{8});
    masked.put(PendingFixture::elements-8,PendingFixture::object);
    assert(masked.sample().available);
    PendingFixture negative; negative.put(PendingFixture::connection+0x84,std::uint32_t{0x80000000});
    assert(negative.sample().available);
    PendingFixture badConnection; assert(!badConnection.sample(UINTPTR_MAX-4).available);
}
void test_intermediate_nested_admission() {
    using F=PendingFixture;
    PendingFixture good; good.admit_intermediate();
    auto result=good.sample();
    assert(result.available && result.callbackRva==0x105BE10 && result.slotKnown && !result.slotPresent);
    assert(result.nestedAvailable && result.nestedCallbackRva==0xB94700);
    assert(std::string(result.nestedReason)=="available");
    PendingFixture slot; slot.admit_intermediate(); slot.put(F::object+8,static_cast<unsigned char>(0));
    assert(slot.sample().slotPresent);
    PendingFixture other; other.failAddress=F::object+8;
    result=other.sample(); assert(result.available && !result.slotKnown && !result.nestedAvailable);
    for(const auto address:{F::connection+0x80,F::pool,F::table,F::resource,F::header+0x1C,
        F::elements+16,F::object,F::vtable,F::image+0x105BE10,F::object+8,F::object+0x10,
        F::elements+48,F::elements+32,F::nestedObject,F::nestedVtable,F::image+0xB94700}) {
        PendingFixture changed; changed.admit_intermediate(); changed.changeAddress=address;
        result=changed.sample(); assert(!result.available && !result.slotKnown && !result.slotPresent);
        assert(!result.nestedAvailable && result.nestedCallbackRva==0 && result.locator==0);
    }
    PendingFixture absent; absent.admit_intermediate(); absent.put(F::object+0x14,UINT32_MAX);
    result=absent.sample(); assert(result.available && result.slotKnown && !result.nestedAvailable);
    assert(std::string(result.nestedReason)=="no_pending_handle" && result.nestedCallbackRva==0);
    PendingFixture key; key.admit_intermediate(); key.put(F::elements+48,std::uint32_t{0});
    result=key.sample(); assert(result.available && !result.nestedAvailable && result.nestedCallbackRva==0);
    assert(std::string(result.nestedReason)=="pending_key");
    PendingFixture index; index.admit_intermediate(); index.put(F::object+0x14,std::uint32_t{2});
    assert(std::string(index.sample().nestedReason)=="element_bounds");
    PendingFixture external; external.admit_intermediate(); external.put(F::nestedVtable,std::uintptr_t{0x70000000});
    result=external.sample(); assert(result.available && !result.nestedAvailable && result.nestedCallbackRva==0);
    assert(std::string(result.nestedReason)=="callback_external");
    for(const auto address:{F::object+8,F::nestedObject,F::nestedVtable,F::image+0xB94700}) {
        PendingFixture missing; missing.admit_intermediate(); missing.failAddress=address;
        result=missing.sample(); assert(result.available && !result.nestedAvailable && result.nestedCallbackRva==0);
    }
    for(const auto address:{F::image+0x105BE10,F::image+0x105C06D,F::image+0x105C0C7}) {
        PendingFixture layout; layout.admit_intermediate(); layout.put(address,static_cast<unsigned char>(0));
        result=layout.sample(); assert(result.available && !result.slotKnown && !result.nestedAvailable);
        assert(std::string(result.nestedReason)=="native_layout");
    }
    PendingFixture masked; masked.admit_intermediate(); masked.put(F::table+0x34,std::int32_t{8});
    masked.put(F::elements+40,std::uintptr_t{8}); masked.put(F::elements+24,F::nestedObject);
    assert(masked.sample().nestedAvailable);
    PendingFixture signedHandle; signedHandle.admit_intermediate();
    signedHandle.put(F::object+0x14,std::uint32_t{0x80000001}); assert(signedHandle.sample().nestedAvailable);
    PendingFixture noRecursion; noRecursion.admit_intermediate();
    noRecursion.put(F::nestedVtable,F::image+0x105BE10); noRecursion.failAddress=F::nestedObject+8;
    result=noRecursion.sample(); assert(result.nestedAvailable && result.nestedCallbackRva==0x105BE10);
}
#endif
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view event) noexcept {
    events.emplace_back(event);
    SetLastError(991);
}
}
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>, const char*) noexcept { return nullptr; }
}
namespace dawn::client::hooking::detour {
bool install(const Spec&, Handle&) noexcept { return false; }
bool uninstall(Handle&) noexcept { return true; }
}
int main() {
    using namespace dawn::client::hooks::queuez;
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    test_pending_owner_snapshot();
    test_intermediate_nested_admission();
#endif
    g_original.store(&original);
    std::array<std::byte, 16> message{};
    for (unsigned count = 0; count < 100; ++count) {
        SetLastError(91);
        assert(handler(nullptr, nullptr, message.data()) == 0);
        assert(GetLastError() == 91);
    }
    assert(originalCalls == 0);
    assert(events.front().find("response_context_index=") != std::string::npos);
    assert(events.front().find("response_kind=") == std::string::npos);
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    assert(events.size() == 18);
    assert(events.front().find("pending_owner=frame_unavailable") != std::string::npos);
    assert(events.front().find("slot_known=0 slot_present=0 nested_owner=not_selected nested_callback_rva=0x0")
        != std::string::npos);
    assert(events.back().find("null_count=64") != std::string::npos);
    unsigned budget = 16;
    for (unsigned exponent = 5; exponent < 64; ++exponent)
        budget += static_cast<unsigned>(trace_null_count(std::uint64_t{1} << exponent));
    assert(budget == 64 && !trace_null_count(0) && !trace_null_count(UINT64_MAX));
#else
    assert(events.size() == 1 && events.front().find("null_count=") == std::string::npos);
    assert(events.front().find("slot_known=") == std::string::npos
        && events.front().find("nested_owner=") == std::string::npos);
#endif
    const std::uintptr_t payload = 123;
    std::memcpy(message.data() + 8, &payload, sizeof payload);
    for (unsigned count = 0; count < 3; ++count)
        assert(handler(nullptr, nullptr, message.data()) == 7);
    assert(originalCalls == 3 && g_processedUpdateGeneration.load() == 3);
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    assert(events.back().find("null_count=100") != std::string::npos);
#endif
    uninstall_null_payload_guard();
    assert(g_processedUpdateGeneration.load() == 0);
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    assert(g_nullPayloadCount.load() == 0);
#endif
    return 0;
}
