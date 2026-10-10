#include "../../dawn/src/server/gameplay/peer/peer_transport.cpp"
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
void test_associations();
void test_dtls();
namespace dawn::server::gameplay::peer {
void regression() {
    CHECK(g_peers.size() == 64);
    std::array<state::gameplay::Endpoint,64> endpoints{};
    for(std::size_t i=0;i<endpoints.size();++i) {
        endpoints[i]={0x7f000001,static_cast<std::uint16_t>(20000+i)};
        wire::ConnectRequest request{}; request.channelId=7;request.sequence=100;
        answer_connect(endpoints[i],request,1000);
        auto* p=find_locked(endpoints[i]);CHECK(p);
        p->stage=state::gameplay::PeerStage::connected;
        p->sessions[0]=100+i;
    }
    CHECK(allocate_locked()==nullptr);
    wire::ConnectRequest request{};request.channelId=7;request.sequence=100;
    const state::gameplay::Endpoint overflow{0x7f000001,21000};
    answer_connect(overflow,request,1001);CHECK(!find_locked(overflow));
    auto* p=find_locked(endpoints[4]);const auto channel=p->localConnectionSequence;
    answer_connect(endpoints[4],request,1002);
    CHECK(p->localConnectionSequence==channel && p->sessions[0]==104);
    CHECK(p->stage==state::gameplay::PeerStage::connected);
    request.channelId=8;answer_connect(endpoints[4],request,1003);
    CHECK(find_locked(endpoints[4])==p && p->sessions[0]==104);
    CHECK(p->localConnectionSequence!=channel && allocate_locked()==nullptr);
    const auto slot=static_cast<std::size_t>(p-g_peers.data());
    for(int round=0;round<64;++round) {
        g_issuedOrdinals[slot]=99;
        drop_endpoint(endpoints[4]);CHECK(!find_locked(endpoints[4]));
        CHECK(g_issuedOrdinals[slot]==0);
        CHECK(!g_nativeStamps[slot].proven);
        answer_connect(endpoints[4],request,1100+round);
        p=find_locked(endpoints[4]);CHECK(p==&g_peers[slot] && p->sessions[0]==0);
        for(std::size_t i=0;i<endpoints.size();++i)if(i!=4)
            CHECK(find_locked(endpoints[i]) && find_locked(endpoints[i])->sessions[0]==100+i);
    }
    for(auto& peer:g_peers)peer.acknowledgementOwed=true;
    service(2000);
    for(const auto& peer:g_peers)CHECK(!peer.acknowledgementOwed);
    for(const auto& endpoint:endpoints)drop_endpoint(endpoint);
    CHECK(std::all_of(g_peers.begin(),g_peers.end(),[](const auto& p){return p.stage==state::gameplay::PeerStage::absent;}));
    std::printf("PASS peer capacity=64; overflow; duplicate connect; reconnect; 64 teardown/reuse cycles; full-capacity poll. PeerLink=%zu bytes\n",sizeof(state::gameplay::PeerLink));
}
}
int main(){ dawn::server::gameplay::peer::regression();test_associations();test_dtls();return 0; }
