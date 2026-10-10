#include "../dawn/src/middleware/gameplay/peer/inbound_ack_context.h"
#include <cassert>
#include <cstdio>
using namespace dawn::middleware::gameplay::peer;
int main() {
    InboundAckContext context{};
    assert(!accept_ack_context(context, false, 0));
    assert(!accept_ack_context(context, true, 1024));
    assert(!context.initialized);
    assert(accept_ack_context(context, true, 20));
    assert(!accept_ack_context(context, true, 20));
    assert(accept_ack_context(context, true, 21));
    assert(!accept_ack_context(context, true, 20));
    assert(context.newestHead == 21);
    assert(!accept_ack_context(context, true, 533));
    assert(context.newestHead == 21);
    // a delayed old packet must not get a new ACK identity after 128 local sends
    for (std::uint16_t head = 22; head != 150; ++head)
        assert(accept_ack_context(context, true, head));
    assert(!accept_ack_context(context, true, 20));
    InboundAckContext wrap{1023, true};
    assert(accept_ack_context(wrap, true, 0));
    assert(!accept_ack_context(wrap, true, 1023));
    InboundAckContext anchored{};
    std::uint16_t resolved = 999;
    assert(resolve_ack_context(anchored, true, 20, 4, 4, resolved) && resolved == 4);
    assert(resolve_ack_context(anchored, true, 21, 4, 132, resolved) && resolved == 4);
    assert(!resolve_ack_context(anchored, true, 20, 5, 132, resolved));
    assert(resolved == 4 && anchored.confirmedBase == 4 && anchored.newestHead == 21);
    assert(!resolve_ack_context(anchored, true, 22, 68, 132, resolved));
    assert(!resolve_ack_context(anchored, true, 22, 5, 4, resolved));
    assert(resolve_ack_context(anchored, true, 22, 5, 132, resolved) && resolved == 5);
    InboundAckContext anchorWrap{1023, true, 1023,1023};
    assert(resolve_ack_context(anchorWrap, true, 0, 0, 1024, resolved) && resolved == 0);
    assert(anchorWrap.confirmedOrdinal==1024);
    for(std::uint64_t issued=512;issued<=3072;++issued){
        InboundAckContext stalled{20,true,4,4};resolved=777;
        assert(!resolve_ack_context(stalled,true,21,4,issued+4,resolved));
        assert(stalled.confirmedOrdinal==4&&resolved==777);
    }
    for (unsigned base = 0; base != 1024; ++base) {
        for (unsigned delta = 0; delta != 1024; ++delta) {
            InboundAckContext sample{static_cast<std::uint16_t>(base), true};
            const auto head = static_cast<std::uint16_t>((base + delta) % 1024);
            const bool accepted = accept_ack_context(sample, true, head);
            assert(accepted == (delta > 0 && delta < 512));
            assert(sample.newestHead == (accepted ? head : base));
        }
    }
    std::puts("inbound ACK context: 1048576 delta cases and replay/wrap checks passed");
}
