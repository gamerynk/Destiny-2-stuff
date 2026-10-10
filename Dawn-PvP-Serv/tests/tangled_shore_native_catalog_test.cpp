#include <fstream>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include "state/build_data/inventory/buckets/definition.h"
#include "state/build_data/socket_entry_lists/definition.h"
#include "state/build_data/cache/records/format.h"
#include "state/activity/tangled_shore/runtime.h"
#include "state/activity/tangled_shore/roster.h"
#include "state/activity/tangled_shore/lost_sector_inventory.h"
#include "state/activity/tangled_shore/spider_presentation.h"
#include "middleware/encoding/bit_writer.h"
#define dawn shore_reference
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/coo/native_combatant_authority.h"
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/coo/open_world_catalog.h"
#include "../../reference/production/Dawn-production/Dawn/src/middleware/bap/activity_message/native/native_npc_animation_authority.h"
#undef dawn
namespace records=dawn::state::build_data::cache::records;
namespace layouts=dawn::state::build_data::scenarios;
namespace shore=dawn::state::activity::tangled_shore;
namespace native=dawn::state::activity::coo::native_combatant;
namespace golden=shore_reference::state::activity::coo::native_combatant;
namespace bits=dawn::middleware::encoding::bits;
static std::vector<layouts::RosterGroup> groups;
static void check(bool value,const char* label){if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
struct Body {std::array<std::byte,85> bytes{};std::size_t count{};bool operator==(const Body&)const=default;};
Body body(const shore::Frame& frame,const shore::PopulationBinding& binding){Body b{};bits::Writer w(b.bytes);check(shore::write_body(w,frame,shore::kRegistries[binding.registry].key,1,binding.source),"write exact native Tangled Shore source");b.count=w.bit_count();return b;}
template<class T> void read(std::ifstream& file,T& value) {file.read(reinterpret_cast<char*>(&value),sizeof value);check(bool(file),"read actual cached record");}
template<class T> void skip(std::ifstream& file,std::uint32_t count) {file.seekg(static_cast<std::streamoff>(sizeof(T)*std::uint64_t(count)),std::ios::cur);}
int main(int argc,char** argv) {
 check(argc==2,"cache path argument");std::ifstream file(argv[1],std::ios::binary);check(bool(file),"open local cache read only");
 records::Prefix prefix{};read(file,prefix);check(prefix.magic==records::kCacheMagic,"cache magic");
 check(prefix.version==records::kCacheFormatVersion,"cache version before current header offsets");
 file.seekg(0,std::ios::beg);records::Header h{};read(file,h);
 check(h.magic==records::kCacheMagic && h.version==records::kCacheFormatVersion,"current cache header");
 skip<records::NamedRecord>(file,h.namedCount);skip<records::ItemRecord>(file,h.itemCount);
 skip<records::CollectibleRecord>(file,h.collectibleCount);skip<records::MaterialRequirementSetRecord>(file,h.materialRequirementSetCount);
 skip<records::ItemDetailRecord>(file,h.itemDetailCount);skip<records::SocketPlugRuleRecord>(file,h.socketPlugRuleCount);
 skip<records::SocketPlugPoolRecord>(file,h.socketPlugPoolCount);skip<records::SocketPlugMemberRecord>(file,h.socketPlugMemberCount);
 skip<records::InventoryBucketRecord>(file,h.inventoryBucketCount);skip<records::SocketEntryListRecord>(file,h.socketEntryListCount);
 skip<records::SocketEntryTableRecord>(file,h.socketEntryTableCount);skip<records::AbilityBucketRecord>(file,h.abilityBucketCount);
 skip<records::ProgressionRecord>(file,h.progressionCount);
 layouts::Definition layout{};bool found{};
 for(std::uint32_t i=0;i<h.scenarioCount;++i) {
  records::ScenarioRecord row{};read(file,row);
  if(std::string_view(row.name.data(),row.nameLength)!=shore::kDestination.activity) continue;
  found=true;layout.bubbleCount=row.bubbleCount;layout.bubbleStates=row.bubbleStates;layout.bubbleHashes=row.bubbleHashes;layout.tag=row.tag;layout.rosterGroupCount=row.rosterGroupCount;layout.rosterGroups=row.rosterGroups;
  layout.bubbleGroupCount=row.bubbleGroupCount;layout.bubbleGroups=row.bubbleGroups;
  for(std::size_t g=0;g<row.bubbleGroupMasks.size();++g) for(std::size_t b=0;b<8;++b)
   layout.bubbleGroupMasks[g]|=std::uint64_t(row.bubbleGroupMasks[g][b])<<(b*8);
 }
 check(found && layout.tag==shore::kDestination.scenario,"real Tangled Shore cached layout");
 groups.resize(h.rosterGroupCount);
 for(auto& group:groups) {records::RosterGroupRecord row{};read(file,row);group.registryKey=row.registryKey;group.objectTag=row.objectTag;
 group.slotCount=row.slotCount;group.slotTypes=row.slotTypes;group.slotFlags=row.slotFlags;group.slotIndices=row.slotIndices;}
 unsigned baseGroups{},players{},missing{},matched{},type1{},slots{},requests{};std::array<unsigned,64> bubbleGroups{};
 for(std::size_t i=0;i<layout.rosterGroupCount;++i){check(layout.rosterGroups[i]<groups.size(),"cached global index");++baseGroups;
  for(std::size_t slot=0;slot<groups[layout.rosterGroups[i]].slotCount;++slot)players+=groups[layout.rosterGroups[i]].slotTypes[slot]==13;}
 baseGroups+=layout.bubbleGroupCount;
 check(players>0,"shipped Tangled Shore base roster preserves player participation");
 for(const auto& registry:shore::kRegistries){++bubbleGroups[registry.bubble];
  check(registry.scenario==layout.tag && registry.activity==shore::kDestination.activity,"catalog scenario provenance");
  check(registry.bubble<layout.bubbleCount&&layout.bubbleHashes[registry.bubble]==registry.bubbleHash,"every Tangled Shore authored bubble matches shipped cache hash");
  bool cached{};for(const auto& row:groups)if(row.registryKey==registry.key&&row.objectTag==registry.objectTag){cached=true;++matched;
   for(const auto& slot:registry.slots){bool same{};for(std::size_t s=0;s<row.slotCount;++s)same|=row.slotIndices[s]==slot.index&&row.slotTypes[s]==slot.type;check(same,"cached authored slot provenance");}}
  missing+=!cached;
  for(const auto& slot:registry.slots){++slots;if(slot.type==1){++type1;check(shore::source_index(registry.key,slot.index)<shore::kPopulations.size()||shore::unselected_source(registry.key,slot.index),"every declared type1 has exact policy or explicit unselected sibling inventory");}}
 }
 for(std::size_t i=0;i<shore::kRegistries.size();++i){const auto& a=shore::kRegistries[i];const auto& b=shore_reference::state::activity::coo::open_world::tangled_shore::kRegistries[i];check(a.key==b.key&&a.objectTag==b.objectTag&&a.bubble==b.bubble&&a.bubbleHash==b.bubbleHash&&a.slots.size()==b.slots.size(),"registry identity exactly production");for(std::size_t j=0;j<a.slots.size();++j){const auto& x=a.slots[j];const auto& y=b.slots[j];check(x.index==y.index&&x.type==y.type&&x.componentClass==y.componentClass&&x.senseSchema==y.senseSchema&&x.authSchema==y.authSchema&&x.descriptorTag==y.descriptorTag,"all authored native descriptors exactly production");}}
 for(std::size_t i=0;i<140;++i){const auto& a=shore::kPopulations[i];const auto& b=shore_reference::state::activity::coo::open_world::tangled_shore::kPopulations[i];check(a.registry==b.registry&&a.source==b.source&&a.rule==b.rule&&a.tactical==b.tactical&&a.tacticalRow==b.tacticalRow&&a.hasRule==b.hasRule&&a.tacticalRows==b.tacticalRows&&a.requestOverride==b.requestOverride&&a.secondRequestOverride==b.secondRequestOverride&&a.categories==b.categories,"every native population policy exactly production");}
 check(shore::kRegistries.size()==56&&type1==153&&shore::kPopulations.size()==141,"complete ordinary Tangled Shore source registry coverage");
 for(const auto count:bubbleGroups)check(count<=shore::roster::wire::kBubbleKeyCapacity,"all Tangled Shore bubble key blocks fit native capacity");
 for(std::size_t i=0;i<shore::kPopulations.size();++i){const auto& b=shore::kPopulations[i];requests+=(b.requestOverride?b.requestOverride:1)+b.secondRequestOverride;for(std::size_t j=0;j<i;++j)check(b.registry!=shore::kPopulations[j].registry||b.source!=shore::kPopulations[j].source,"no duplicate source ownership");}
 check(shore::kPopulations.size()<=160,"ordinary source list fits existing wire; Lost Sectors require more");
 check(baseGroups+shore::kRegistries.size()<=shore::roster::wire::kGroupCapacity,"ordinary complete Shore fits group capacity");
 auto storage=std::make_unique<shore::roster::Storage>();shore::roster::wire::Roster creator{},derived{};
 check(shore::roster::compose(layout,groups,*storage,creator,true)&&creator.groupCount==58,"all56 ordinary registries plus base2 published");
 check(shore::roster::compose(layout,groups,*storage,derived,false)&&derived.groupCount==baseGroups&&derived.playerKeyGroup,"derived native base projection remains valid");
 unsigned lostSources{};const auto lost=[&](const auto& registry){check(registry.bubble<layout.bubbleCount&&layout.bubbleHashes[registry.bubble]==registry.bubbleHash,"staged Lost Sector bubble matches shipped cache");for(const auto& ordinary:shore::kRegistries)check(registry.key!=ordinary.key,"staged sector never ambient activation");for(const auto& slot:registry.slots)lostSources+=slot.type==1;};
 for(const auto& registry:shore::lost_sector_inventory::kRegistries)lost(registry);for(const auto& registry:shore::lost_sector_inventory::kRewardRegistries)lost(registry);
 check(lostSources==111&&shore::lost_sector_inventory::kRegistries.size()==10&&shore::lost_sector_inventory::kRewardRegistries.size()==5,"complete ten combat and five reward sector inventories");
 check(shore::kPopulations.size()+lostSources>160&&baseGroups+56+15>64,"full staged Shore exceeds current wire/roster limits");
 shore::Runtime runtime;const shore::RuntimeBinding identity{100,7,true};
 check(!runtime.update(identity,56,false).enabled,"no initial source bodies before native arrival");
 const auto first=runtime.update(identity,56,true);unsigned initial{};
 for(const auto& source:first.sources)initial+=source.generation!=0;
 check(initial>0&&initial<141,"arrival activates only native initial bubble");
 const auto prewarm=runtime.update(identity,56,false,168);
 check(prewarm.region==56&&prewarm.enabled,"prefetch cannot replace held native region");
 unsigned warmed{};for(std::size_t i=0;i<shore::kPopulations.size();++i)if(shore::kRegistries[shore::kPopulations[i].registry].bubble==21)warmed+=prewarm.sources[i].generation!=0;
 check(warmed==0,"prefetch does not activate vendor-only bubble");
 shore::Runtime npcPrewarm;(void)npcPrewarm.update(identity,56,true);const auto beforeNpc=npcPrewarm.update(identity,56,false,168);
 for(std::size_t i=0;i<shore::kPopulations.size();++i)if(shore::kPopulations[i].kind==shore::PopulationKind::npc)check(beforeNpc.sources[i].generation==0,"prefetch excludes Spider and guards until actual vendor bubble arrival");
 for(const auto& registry:shore::kRegistries)(void)runtime.update(identity,registry.bubble*8U,true);
 const auto all=runtime.update(identity,56,true);unsigned twoCategories{};
 for(const auto& unsupported:shore::kUnselectedSources)check(!shore::body_bits(all,shore::kRegistries[unsupported.registry].key,1,unsupported.slot),"unselected sibling never gets fabricated native authority");
 for(std::size_t i=0;i<shore::kPopulations.size();++i){const auto& binding=shore::kPopulations[i];const auto& source=all.sources[i];
  bool rule=!binding.hasRule,objective=binding.tacticalRow<0;
  for(const auto& slot:shore::kRegistries[binding.registry].slots){
   rule|=slot.index==binding.rule&&slot.type==66;
   objective|=slot.index==binding.tactical&&slot.type==3;
  }
  check(rule&&objective,"exact source rule and tactical objective dependencies declared");
  check(binding.categories==1||binding.categories==2,"reviewed one/two category native source policy");
  check(source.generation==1,"visited source retains original native generation");
  const auto actual=body(all,binding);Body expected{};bits::Writer writer(expected.bytes);
  golden::Source reference{shore::kRegistries[binding.registry].key,source.generation,binding.rule,source.first};
  reference.hasRule=binding.hasRule;reference.hasSecondCategory=binding.categories==2;reference.secondRequested=source.second;
  if(binding.tacticalRow>=0)reference.tactical={reference.registry,binding.tactical,binding.tacticalRow,source.generation};
  check(golden::write_source(writer,reference),"production native codec accepts exact Tangled Shore authority");expected.count=writer.bit_count();
  check(actual==expected,"all 141 source bodies byte-identical to production native codec");
  check(actual.count==(binding.categories==2?673U:641U),"native category schema width");twoCategories+=binding.categories==2;
 }
 for(unsigned crossing=0;crossing<30;++crossing){(void)runtime.update(identity,168,true);const auto returned=runtime.update(identity,56,true);
  for(std::size_t i=0;i<shore::kPopulations.size();++i)check(body(returned,shore::kPopulations[i])==body(all,shore::kPopulations[i]),"zone return never replays a new source epoch");}
 check(runtime.update(identity,57,true).region==56,"misaligned native report retains held region");
 check(!runtime.update({100,7,false},56,true).enabled&&runtime.snapshot(identity).visited==all.visited,"derived public binding cannot mutate creator source lifetime");
 bool adapted{};
 for(std::size_t i=0;i<shore::kPopulations.size();++i)if(shore::kPopulations[i].tacticalRows>1) {
  const auto& binding=shore::kPopulations[i];const auto key=shore::kRegistries[binding.registry].key;
  dawn::state::activity::coo::TaskCosts report{};report.initialized=true;report.hasRevision=true;report.revision=1;
  report.mask=3;report.cost[0]=90;report.cost[1]=1;
  check(!runtime.observe_tasks({100,7,false},key,binding.source,1,report),"derived host cannot select tactical rows");
  check(!runtime.observe_tasks({100,8,true},key,binding.source,1,report),"foreign incarnation cannot select tactical rows");
  check(runtime.observe_tasks(identity,key,binding.source,1,report)&&runtime.snapshot(identity).sources[i].tacticalRow==1,"native cost selection chooses reachable authored tactical row");
  report.mask=1;report.cost[0]=0;
  check(runtime.observe_tasks(identity,key,binding.source,1,report)&&runtime.snapshot(identity).sources[i].tacticalRow==0,"sparse native cost reports retain other known rows");
  report.revision=2;
  check(!runtime.observe_tasks(identity,key,binding.source,1,report)&&runtime.snapshot(identity).sources[i].tacticalRow==0,"mismatched evaluator revision cannot change authority");
  adapted=true;break;
 }
 check(adapted,"exercise actual Tangled Shore objective with multiple authored rows");
 check(runtime.update({100,8,true},168,true).visited==(std::uint64_t{1}<<21),"new committed incarnation clears previous source lifetime");
 const auto spider=shore::source_index(shore::spider_presentation::kRegistry,1);check(spider<shore::kPopulations.size()&&shore::kPopulations[spider].rule==9&&all.sources[spider].first==1,"exact additional vendor source has one Spider and declared rule9");
 Body idle{},idleGolden{};bits::Writer idleWriter(idle.bytes),goldenWriter(idleGolden.bytes);
 check(shore::spider_presentation::write_body(idleWriter,all,0x6D47E9B3U,42,8),"native Spider retained idle");idle.count=idleWriter.bit_count();
 check(shore_reference::middleware::bap::activity_message::native::npc_animation::write(goldenWriter,{0x681C2A3AU,0x811C9DC5U,1}),"production Spider performance codec");idleGolden.count=goldenWriter.bit_count();check(idle==idleGolden&&idle.count==101,"Spider tuple byte exact production");
 check(!shore::spider_presentation::body_bits(beforeNpc,0x6D47E9B3U,42,8)&&!shore::spider_presentation::body_bits(all,0x6D47E9B3U,42,7),"only real spawned Spider idle controller authorized");
 std::printf("PASS Tangled Shore real cache v%u registries=56 sources=141 slots=%u base_groups=%u cached_authored=%u fallback_needed=%u initial_sources=%u two_categories=%u requested=%u\n",h.version,slots,baseGroups,matched,missing,initial,twoCategories,requests);
}
