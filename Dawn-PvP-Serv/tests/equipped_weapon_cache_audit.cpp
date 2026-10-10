#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "state/build_data/inventory/buckets/definition.h"
#include "state/build_data/socket_entry_lists/definition.h"
#include "state/build_data/cache/records/format.h"
namespace r=dawn::state::build_data::cache::records;
template<class T> void read(std::ifstream& f,T& x){f.read(reinterpret_cast<char*>(&x),sizeof x);if(!f)std::exit(1);}
template<class T> void skip(std::ifstream& f,std::uint32_t n){f.seekg(static_cast<std::streamoff>(sizeof(T)*std::uint64_t(n)),std::ios::cur);}
int main(int argc,char** argv){
 if(argc!=2)return 1;std::ifstream f(argv[1],std::ios::binary);r::Header h{};read(f,h);
 if(h.magic!=r::kCacheMagic)return 1;
 std::printf("constants extracted=%u light_stat_row=%u character_rows=",h.constants.extracted,h.constants.lightStatRow);
 for(auto row:h.constants.characterStatRows)std::printf("%u,",row);std::puts("");
 skip<r::NamedRecord>(f,h.namedCount);skip<r::ItemRecord>(f,h.itemCount);
 skip<r::CollectibleRecord>(f,h.collectibleCount);skip<r::MaterialRequirementSetRecord>(f,h.materialRequirementSetCount);
 unsigned found{};
 for(std::uint32_t i=0;i<h.itemDetailCount;++i){r::ItemDetailRecord d{};read(f,d);
  if(d.definitionHash!=0xE516CF40U&&d.definitionHash!=0x4156C94DU&&d.definitionHash!=0x6F22FCECU)continue;
  ++found;std::printf("weapon=%08X index=%u slot=%d instanced=%u sockets=%u stat_count=%u stats=",d.definitionHash,d.definitionIndex,d.equipmentSlot,d.instancedDefinition,d.ordinarySocketCount,d.statCount);
  for(unsigned j=0;j<d.statCount;++j)std::printf("%s%u:%d",j?",":"",d.statRows[j],d.statValues[j]);
  std::puts("");
 }
 return found==3?0:1;
}
