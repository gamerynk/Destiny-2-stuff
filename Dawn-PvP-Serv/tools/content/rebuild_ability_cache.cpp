#include "client/content/items/packages/internal.h"
#include "state/build_data/cache/records/format.h"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <tuple>
namespace fs=std::filesystem;
namespace cr=dawn::state::build_data::cache::records;
namespace d=dawn::state::build_data::abilities;
static fs::path root;
std::vector<std::byte> load(const fs::path&p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw 1;auto n=f.tellg();std::vector<std::byte>b(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);if(!f)throw 2;return b;}
namespace dawn::middleware::content::packages::reader {
bool read_tag(const Source&,Scratch&,std::uint32_t tag,std::vector<std::byte>&out) noexcept {try{char n[32];sprintf_s(n,"%08X.bin",tag);out=load(root/"pools"/n);return true;}catch(...){return false;}}
}
int main(int argc,char**argv){try{
 if(argc!=2)return 1;root=argv[1];auto b=load(root/"build_data.original.bin");cr::Header h{};if(b.size()<sizeof h)return 2;memcpy(&h,b.data(),sizeof h);if(h.magic!=cr::kCacheMagic||h.version!=43)return 3;
 uint64_t sum=cr::kChecksumOffsetBasis;for(size_t i=offsetof(cr::Header,constants);i<offsetof(cr::Header,payloadChecksum);++i){sum^=std::to_integer<unsigned>(b[i]);sum*=cr::kChecksumPrime;}for(size_t i=sizeof h;i<b.size();++i){sum^=std::to_integer<unsigned>(b[i]);sum*=cr::kChecksumPrime;}if(sum!=h.payloadChecksum)return 4;
 size_t off=sizeof h;
 #define SKIP(N,T) off+=size_t(h.N)*sizeof(cr::T)
 SKIP(namedCount,NamedRecord);SKIP(itemCount,ItemRecord);SKIP(collectibleCount,CollectibleRecord);SKIP(materialRequirementSetCount,MaterialRequirementSetRecord);SKIP(itemDetailCount,ItemDetailRecord);SKIP(socketPlugRuleCount,SocketPlugRuleRecord);SKIP(socketPlugPoolCount,SocketPlugPoolRecord);SKIP(socketPlugMemberCount,SocketPlugMemberRecord);SKIP(inventoryBucketCount,InventoryBucketRecord);SKIP(socketEntryListCount,SocketEntryListRecord);SKIP(socketEntryTableCount,SocketEntryTableRecord);
 if((h.abilityBucketCount==0||h.abilityBucketCount>512)||off+h.abilityBucketCount*sizeof(cr::AbilityBucketRecord)>b.size())return 5;
 static dawn::middleware::content::packages::reader::Source source{};
 static dawn::middleware::content::packages::reader::Scratch scratch{};
 size_t changed=0;std::vector<std::byte> blob;
 for(unsigned i=0;i<h.abilityBucketCount;++i){cr::AbilityBucketRecord a{};auto pos=off+i*sizeof a;memcpy(&a,b.data()+pos,sizeof a);
 d::Definition out{};out.socketEntryListIndex=a.socketEntryListIndex;out.selection={a.movementEntry,a.grenadeEntry,a.superEntry,a.meleeEntry,a.classEntry};
 auto list=load(root/("subclass-list-"+std::to_string(a.socketEntryListIndex)+".bin"));
 if(!dawn::client::content::items::packages::build_ability_buckets(source,scratch,list,blob,out.selection,out))return 6;
 auto old=a;a.overflowCount=out.overflowCount;a.overflow=out.overflow;
 for(size_t k=0;k<out.buckets.size();++k){a.bucketKinds[k]=out.buckets[k].kind;a.bucketHashCounts[k]=out.buckets[k].hashCount;for(size_t j=0;j<16;++j)a.bucketHashes[k*16+j]=out.buckets[k].hashes[j];}
 if(memcmp(&a,&old,sizeof a)){++changed;printf("changed list=%u melee=%u class=%u\n",a.socketEntryListIndex,a.meleeEntry,a.classEntry);}
 memcpy(b.data()+pos,&a,sizeof a);
 }

 std::vector<cr::AbilityBucketRecord> additions;
 for(unsigned li:{1,2,3,5,6,7,9,10,11})for(unsigned mv:{4,5,6})for(unsigned gr:{7,8,9})for(unsigned cl:{2,3}){
 d::Definition out{};out.selection={uint8_t(mv),uint8_t(gr),10,21,uint8_t(cl)};
 auto list=load(root/("subclass-list-"+std::to_string(li)+".bin"));
 auto primary=out.selection;primary.superEntry=20;
 if(dawn::client::content::items::packages::build_ability_buckets(source,scratch,list,blob,primary,out))continue;
 if(!dawn::client::content::items::packages::build_ability_buckets(source,scratch,list,blob,out.selection,out))return 8;
 cr::AbilityBucketRecord a{};a.socketEntryListIndex=uint16_t(li);a.movementEntry=uint8_t(mv);a.grenadeEntry=uint8_t(gr);a.classEntry=uint8_t(cl);a.superEntry=10;a.meleeEntry=21;
 a.overflowCount=out.overflowCount;a.overflow=out.overflow;
 for(size_t k=0;k<12;++k){a.bucketKinds[k]=out.buckets[k].kind;a.bucketHashCounts[k]=out.buckets[k].hashCount;for(size_t j=0;j<16;++j)a.bucketHashes[k*16+j]=out.buckets[k].hashes[j];}
 bool exists=false;for(unsigned i=0;i<h.abilityBucketCount;++i)if(!memcmp(b.data()+off+i*sizeof a,&a,7)){exists=true;break;}
 if(!exists)additions.push_back(a);
 }
 if(h.abilityBucketCount+additions.size()!=486)return 9;
 const auto* begin=reinterpret_cast<const std::byte*>(additions.data());
 b.insert(b.begin()+off+h.abilityBucketCount*sizeof(cr::AbilityBucketRecord),begin,begin+additions.size()*sizeof(cr::AbilityBucketRecord));
 h.abilityBucketCount+=static_cast<unsigned>(additions.size());
 // the native cache loader requires strictly ordered keys; unsorted rows invalidate the whole cache
 std::vector<cr::AbilityBucketRecord> ordered(h.abilityBucketCount);
 memcpy(ordered.data(), b.data()+off, ordered.size()*sizeof(ordered[0]));
 const auto key=[](const cr::AbilityBucketRecord& a){
     return std::tuple(a.socketEntryListIndex,a.movementEntry,a.grenadeEntry,
                       a.superEntry,a.meleeEntry,a.classEntry);
 };
 std::sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& c){return key(a)<key(c);});
 for(size_t i=1;i<ordered.size();++i)if(!(key(ordered[i-1])<key(ordered[i])))return 11;
 memcpy(b.data()+off, ordered.data(), ordered.size()*sizeof(ordered[0]));
 printf("added missing fallback selections=%zu\n",additions.size());
 sum=cr::kChecksumOffsetBasis;for(size_t i=offsetof(cr::Header,constants);i<offsetof(cr::Header,payloadChecksum);++i){sum^=std::to_integer<unsigned>(b[i]);sum*=cr::kChecksumPrime;}for(size_t i=sizeof h;i<b.size();++i){sum^=std::to_integer<unsigned>(b[i]);sum*=cr::kChecksumPrime;}h.payloadChecksum=sum;memcpy(b.data(),&h,sizeof h);
 std::ofstream f(root/"build_data.bin",std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());if(!f)return 7;
 printf("PASS rebuilt=%u changed=%zu ability_offset=%zu record_bytes=%zu header=%zu checksum=%016llX\n",h.abilityBucketCount,changed,off,sizeof(cr::AbilityBucketRecord),sizeof h,sum);return 0;
 }catch(...){return 10;}}
