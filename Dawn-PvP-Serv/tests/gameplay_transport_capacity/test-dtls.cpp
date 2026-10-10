#include "../../dawn/src/server/gameplay/dtls/dtls_host.cpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL DTLS line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
void test_dtls(){
 using namespace dawn::server::gameplay::dtls;
 CHECK(g_associations.size()==256);
 for(std::size_t i=0;i<64;++i)for(std::size_t j=0;j<4;++j){
  dawn::state::gameplay::Endpoint e{0x7f000001,static_cast<std::uint16_t>(23000+i)};auto id=security_id(j+1);
  auto* a=acquire(e,id,1000);CHECK(a && a->stage==Stage::absent);
  a->endpoint=e;a->securityId=id;a->stage=Stage::established;a->touched=1000;
 }
 for(std::size_t i=0;i<64;++i)for(std::size_t j=0;j<4;++j){
  dawn::state::gameplay::Endpoint e{0x7f000001,static_cast<std::uint16_t>(23000+i)};auto id=security_id(j+1);
  auto* a=find_handshake(e,id);CHECK(a && acquire(e,id,1001)==a);
 }
 reset();for(const auto& a:g_associations)CHECK(a.stage==Stage::absent);
 std::puts("PASS DTLS: 64 endpoints x 4 simultaneous security associations retained; retries reuse slots; reset.");
}
