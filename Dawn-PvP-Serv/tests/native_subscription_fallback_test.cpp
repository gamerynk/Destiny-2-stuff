#include <array>
#include <cstdio>
#include "../dawn/src/server/bap/encrypted/queuez/queuez_state_validation.h"
#include "../dawn/src/server/bap/encrypted/queuez/staging/queuez_family_staging.h"
#include "../dawn/src/middleware/web_service/web_service_envelope.h"
int main() {
    namespace q=dawn::server::bap::encrypted::queuez;
    namespace ws=dawn::middleware::web_service;
    const dawn::middleware::queuez::Subscription subscription{3,0x40};
    q::SessionState before{},after{};bool publish{};
    if (!q::stage_family3_subscription(before,subscription,publish,after) || !publish || !after.family3Active) return 1;
    before=after;
    if (!q::stage_family3_subscription(before,subscription,publish,after) || !publish || after.family3Version!=0) return 2;
    before.family4Active=true;before.family4RootSoid=0x40;before.family4Version=1;
    before.family4ResidentCount=1;before.family4Residents[0]={0x40,1};
    before.family3Phase=q::Family3Phase::publishOnce;
    if (!q::valid(before) || !q::stage_family3_subscription(before,subscription,publish,after)
        || !publish || after.family3Phase!=q::Family3Phase::responseOnly) return 3;
    before=after;
    if (!q::stage_family3_subscription(before,subscription,publish,after) || publish
        || !q::staging::same_state(before,after)) return 4;
    const dawn::middleware::queuez::Subscription wrongRoot{3,0x41};
    if (q::stage_family3_subscription(before,wrongRoot,publish,after) || publish) return 5;
    q::stage_unsubscription(before,3,0x40,after);
    if (!q::valid(after) || after.family3Active || after.family3RootSoid!=0) return 6;
    before=after;
    if (!q::stage_family3_subscription(before,subscription,publish,after) || !publish || !after.family3Active) return 7;
    q::stage_unsubscription(after,3,0x41,before);
    if (!q::staging::same_state(before,after)) return 8;
    std::array<std::byte,32> response{};std::size_t size{};
    if (!ws::encode_response({206,1,{}},ws::ResponseShape::statusOnly,{},response,size)
        || size!=7 || response[6]!=std::byte{8}) return 9;
    std::puts("PASS: actual subscription/retry/publishOnce/suppression/invalid-root/unsubscribe/resubscribe decisions; raw absent-source envelope is unsafe and must be refused by encrypted runtime");
}
