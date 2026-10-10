#include <array>
#include <cstdio>
#include <limits>
#include "../dawn/src/server/bap/encrypted/queuez/staging/queuez_subscription_progress.h"
#include "../dawn/src/server/bap/encrypted/queuez/staging/queuez_family_staging.h"
namespace q=dawn::server::bap::encrypted::queuez;
int main() {
    q::SessionState before{};
    before.family3Active=true;before.family3RootSoid=0x40;before.family3Version=7;
    before.family4Active=true;before.family4RootSoid=0x40;before.family4Version=3;
    before.family4ResidentCount=2;before.family4Residents[0]={0x40,1};before.family4Residents[1]={0x41,2};
    before.family3Phase=q::Family3Phase::responseOnly;
    const dawn::middleware::queuez::Subscription subscription{3,0x40};
    std::array<std::byte,25> output{};std::size_t written{};q::SessionState after{};
    if (!q::prepare_family3_subscription_progress(before,subscription,output,written,after) || written!=25) return 1;
    auto expected=before;expected.family3Version=8;
    if (!q::staging::same_state(expected,after)) return 2;
    // wire contract: one Family3, root 40, next version 8, flags 0, zero objects
    const std::array<std::byte,25> wire{std::byte{0},std::byte{0},std::byte{0},std::byte{1},
        std::byte{0},std::byte{0},std::byte{0},std::byte{3},
        std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0x40},
        std::byte{0},std::byte{0},std::byte{0},std::byte{8},std::byte{0},
        std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
    if (output!=wire) return 3;
    if (q::prepare_family3_subscription_progress(before,subscription,std::span(output).first(24),written,after)
        || written || !q::staging::same_state(before,after)) return 4;
    for (int reason=0;reason<6;++reason) {
        auto candidate=before;auto requested=subscription;
        if (reason==0) candidate.family3Version=(std::numeric_limits<std::int32_t>::max)();
        if (reason==1) candidate.family3Phase=q::Family3Phase::normal;
        if (reason==2) candidate.family3Phase=q::Family3Phase::publishOnce;
        if (reason==3) requested.familyRootSoid=0x41;
        if (reason==4) requested.familyType=0;
        if (reason==5) candidate.family3RootSoid=0;
        if (q::prepare_family3_subscription_progress(candidate,requested,output,written,after) || written) return 5+reason;
        if (after.family3Version!=candidate.family3Version || after.family3RootSoid!=candidate.family3RootSoid) return 11+reason;
    }
    for (int index=0;index<3;++index) {
        if (!q::prepare_family3_subscription_progress(before,subscription,output,written,after)) return 17;
        expected=before;expected.family3Version++;
        if (!q::staging::same_state(expected,after)) return 18;
        before=after;
    }
    std::puts("PASS: real Family3 next-version25-byte incremental, resident preservation, transactional output failure, refusal and ordered retry cases");
}
