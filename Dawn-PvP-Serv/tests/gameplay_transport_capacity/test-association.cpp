#include "../../dawn/src/server/gameplay/association/association_host.cpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL association line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
void test_associations(){
 using namespace dawn::server::gameplay::association;
 using namespace dawn::state::gameplay;
 CHECK(g_state.associations.size()==64);
 for(std::size_t i=0;i<64;++i){auto* a=allocate_locked();CHECK(a);a->endpoint={0x7f000001,static_cast<std::uint16_t>(22000+i)};a->stage=AssociationStage::established;a->lastTick=1000;a->protection.key.fill(std::byte{0x55});}
 CHECK(!allocate_locked());
 auto* reused=&g_state.associations[5];drop_locked(*reused);CHECK(allocate_locked()==reused);
 for(const auto value:reused->protection.key)CHECK(value==std::byte{});
 expire(11001);CHECK(allocate_locked()==&g_state.associations[0]);
 for(const auto& a:g_state.associations)CHECK(a.stage==AssociationStage::absent);
 std::puts("PASS protected association capacity=64; key wipe; expiry and slot reuse.");
}
