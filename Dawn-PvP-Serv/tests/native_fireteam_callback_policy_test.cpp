#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "client/hooks/bootflow/native_fireteam_callback_policy.h"

namespace p = dawn::client::hooks::bootflow::native_fireteam_callback_policy;
static bool check(bool ok, const char* name) {
    if (!ok) std::fprintf(stderr, "FAIL: %s\n", name);
    return ok;
}
int main() {
    bool ok = true;
    std::array<std::byte, p::kOrderedCalls.size()> code{};
    for (std::size_t i = 0; i < code.size(); ++i) code[i] = std::byte{p::kOrderedCalls[i]};
    ok &= check(p::ordered_calls_match(code), "native after-manager call sequence");
    for (std::size_t i = 0; i < code.size(); ++i) {
        code[i] ^= std::byte{1};
        ok &= check(!p::ordered_calls_match(code), "every changed ordering/signature byte rejected");
        code[i] ^= std::byte{1};
    }
    ok &= check(!p::ordered_calls_match({code.data(), code.size()-1}), "truncated sequence rejected");
    for (const std::uintptr_t base : {0x140000000ULL, 0x7FF659D80000ULL}) {
        constexpr std::size_t size = 0x9000000;
        std::array<std::uintptr_t, 10> chain{};
        chain[0] = 0x70000000000ULL;
        for (std::size_t i = 0; i < p::kObservedChain.size(); ++i) chain[i+1] = base+p::kObservedChain[i];
        chain[8] = base-1;
        chain[9] = base+size;
        ok &= check(p::owns_network_frame(chain, base, size), "logical scheduler ownership across ASLR and DLL frames");
        for (std::size_t i = 1; i <= p::kObservedChain.size(); ++i) {
            ++chain[i];
            ok &= check(!p::owns_network_frame(chain, base, size), "changed native caller rejected");
            --chain[i];
        }
        std::swap(chain[2],chain[3]);
        ok &= check(!p::owns_network_frame(chain,base,size), "before-manager or reordered caller rejected");
        std::swap(chain[2],chain[3]);
        ok &= check(!p::owns_network_frame({chain.data(),7},base,size), "partial scheduler chain rejected");
        chain[9] = base+1;
        ok &= check(!p::owns_network_frame(chain,base,size), "additional recursive native frame rejected");
    }
    std::array<std::uintptr_t,17> tooLong{};
    ok &= check(!p::owns_network_frame(tooLong,1,1), "bounded stack only");
    ok &= check(!p::owns_network_frame({},1,1), "empty stack rejected");
    p::CallbackDepth workerA{}, workerB{};
    ok &= check(!workerA.is_outermost(), "observer outside export is rejected");
    ok &= check(!workerA.admit(), "outside callback rejected");
    workerA.begin(); workerA.begin();
    ok &= check(!workerA.is_outermost(), "observer nested export is rejected");
    ok &= check(!workerA.admit(), "nested callback rejected");
    workerA.end();
    ok &= check(workerA.is_outermost() && !workerA.captured,
                "observer outermost query does not consume capture");
    ok &= check(workerA.admit() && !workerA.admit(), "one capture per outer callback");
    ok &= check(workerA.is_outermost() && workerA.captured,
                "observer query preserves already consumed capture");
    workerA.begin(); workerA.end();
    ok &= check(!workerA.admit(), "recursive callback cannot reset outer capture");
    workerA.end(); workerA.begin();
    ok &= check(workerA.admit(), "next logical frame admitted");
    workerB.begin();
    ok &= check(workerB.admit(), "scheduler worker migration admitted independently");
    workerA.end(); workerB.end(); workerB.end();
    ok &= check(workerA.depth==0 && workerB.depth==0, "balanced and excess scope cleanup");
    if (ok) std::puts("PASS: native fireteam callback ownership, ordering, recursion and migration");
    return ok ? 0 : 1;
}
