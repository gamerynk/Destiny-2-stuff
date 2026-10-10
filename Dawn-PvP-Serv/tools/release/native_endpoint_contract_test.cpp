#include <cstdio>
#include <cstring>
#include "../../dawn/src/client/network/native_routes/contract.h"
using dawn::core::settings::Settings;
namespace routes=dawn::client::network::native_routes;
static unsigned checks{};
static bool check(bool ok,const char* label){++checks;if(!ok)std::fprintf(stderr,"FAIL:%s\n",label);return ok;}
static void publish(Settings& settings) {
    auto& external=settings.client.externalServer;
    external.enabled=true; external.host={};
    constexpr char host[]="192.0.2.10";
    std::memcpy(external.host.data(),host,sizeof(host)); external.address={192,0,2,10};
    external.portRouteCount=1; external.portRoutes[0]={30975,{192,0,2,10},30977};
    settings.server.hasBapAdvertisedAddress=true;settings.server.bapAdvertisedAddress={192,0,2,10};
    settings.server.bapPort=30974;settings.server.bapAdvertisedPort=30974;
    settings.server.gameplay.topology=dawn::core::settings::server::gameplay::Topology::embedded;
    settings.server.gameplay.advertisedAddress={192,0,2,10};
    settings.server.gameplay.transportAddress={192,0,2,10};settings.server.gameplay.port=30976;
}
int main(){
    Settings local{},priorRemote{};
    publish(local);publish(priorRemote);
    bool pass=check(routes::valid_contract(local),"local client's merged endpoints satisfy actual contract");
    pass&=check(routes::valid_contract(priorRemote),"prior private server client's merged endpoints satisfy actual contract");
    local.server.hasBapAdvertisedAddress=false;
    pass&=check(!routes::valid_contract(local),"external-only merge is insufficient");publish(local);
    local.server.gameplay.transportAddress={127,0,0,1};
    pass&=check(!routes::valid_contract(local),"gameplay transport must match advertised endpoint");publish(local);
    local.client.externalServer.portRouteCount=2;local.client.externalServer.portRoutes[1]={30974,{192,0,2,10},40074};
    pass&=check(!routes::valid_contract(local),"a stale BAP port override is rejected");publish(local);
    local.server.bapPort=0;pass&=check(!routes::valid_contract(local),"zero relay listener cannot enable native routes");
    if(pass)std::printf("PASS%u actual native-route contract checks\n",checks);
    return pass?0:1;
}
