#include <fstream>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include "state/build_data/inventory/buckets/definition.h"
#include "state/build_data/socket_entry_lists/definition.h"
#include "state/build_data/cache/records/format.h"
#include "state/activity/titan/runtime.h"
#include "state/activity/titan/roster.h"
#include "state/activity/titan/sloane_presentation.h"
#include "middleware/encoding/bit_writer.h"
#define dawn titan_reference
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/coo/native_combatant_authority.h"
#include "../../reference/production/Dawn-production/Dawn/src/middleware/bap/activity_message/native/native_npc_animation_authority.h"
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/vendors/catalog.h"
#undef dawn
namespace records=dawn::state::build_data::cache::records;
namespace layouts=dawn::state::build_data::scenarios;
namespace titan=dawn::state::activity::titan;
namespace native=dawn::state::activity::coo::native_combatant;
namespace golden=titan_reference::state::activity::coo::native_combatant;
namespace bits=dawn::middleware::encoding::bits;
static std::vector<layouts::RosterGroup> groups;
static void check(bool value,const char* label){if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
struct Body {std::array<std::byte,85> bytes{};std::size_t count{};bool operator==(const Body&)const=default;};
Body body(const titan::Frame& frame,const titan::PopulationBinding& binding){Body b{};bits::Writer w(b.bytes);check(titan::write_body(w,frame,titan::kRegistries[binding.registry].key,1,binding.source),"write exact native Titan source");b.count=w.bit_count();return b;}
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
  if(std::string_view(row.name.data(),row.nameLength)!=titan::kDestination.activity) continue;
  found=true;layout.bubbleCount=row.bubbleCount;layout.bubbleStates=row.bubbleStates;layout.bubbleHashes=row.bubbleHashes;layout.tag=row.tag;layout.rosterGroupCount=row.rosterGroupCount;layout.rosterGroups=row.rosterGroups;
  layout.bubbleGroupCount=row.bubbleGroupCount;layout.bubbleGroups=row.bubbleGroups;
  for(std::size_t g=0;g<row.bubbleGroupMasks.size();++g) for(std::size_t b=0;b<8;++b)
   layout.bubbleGroupMasks[g]|=std::uint64_t(row.bubbleGroupMasks[g][b])<<(b*8);
 }
 check(found && layout.tag==titan::kDestination.scenario,"real Titan cached layout");
 groups.resize(h.rosterGroupCount);
 for(auto& group:groups) {records::RosterGroupRecord row{};read(file,row);group.registryKey=row.registryKey;group.objectTag=row.objectTag;
 group.slotCount=row.slotCount;group.slotTypes=row.slotTypes;group.slotFlags=row.slotFlags;group.slotIndices=row.slotIndices;}
 unsigned baseGroups{},players{},missing{},matched{},type1{},slots{};
 for(std::size_t i=0;i<layout.rosterGroupCount;++i){check(layout.rosterGroups[i]<groups.size(),"cached global index");++baseGroups;
  for(std::size_t slot=0;slot<groups[layout.rosterGroups[i]].slotCount;++slot)players+=groups[layout.rosterGroups[i]].slotTypes[slot]==13;}
 baseGroups+=layout.bubbleGroupCount;
 check(players>0,"shipped Titan base roster preserves player participation");
 for(const auto& registry:titan::kRegistries){
  check(registry.scenario==layout.tag && registry.activity==titan::kDestination.activity,"catalog scenario provenance");
  check(registry.bubble<layout.bubbleCount&&layout.bubbleHashes[registry.bubble]==registry.bubbleHash,"every Titan authored bubble matches shipped cache hash");
  bool cached{};for(const auto& row:groups)if(row.registryKey==registry.key&&row.objectTag==registry.objectTag){cached=true;++matched;
   for(const auto& slot:registry.slots){bool same{};for(std::size_t s=0;s<row.slotCount;++s)same|=row.slotIndices[s]==slot.index&&row.slotTypes[s]==slot.type;check(same,"cached authored slot provenance");}}
  missing+=!cached;
  for(const auto& slot:registry.slots){++slots;if(slot.type==1){++type1;check(titan::source_index(registry.key,slot.index)<titan::kPopulations.size(),"every type1 slot has real population policy");}}
 }
 check(titan::kRegistries.size()==32&&type1==93&&titan::kPopulations.size()==93,"complete ordinary Titan source registry coverage");
 check(baseGroups+32<=64,"Titan ordinary creator fits current group capacity");
 auto storage=std::make_unique<titan::roster::Storage>();
 titan::roster::wire::Roster creator{},derived{};
 check(titan::roster::compose(layout,groups,*storage,creator,true),"real cache plus exact32 authored registry projection");
 check(creator.groupCount==34 && creator.bubbleSubBlocks.size()==6,"actual Titan creator34 groups and6 native subblocks");
 const auto playerKey=creator.playerKeyGroup;
 auto derivedStorage=std::make_unique<titan::roster::Storage>();
 check(titan::roster::compose(layout,groups,*derivedStorage,derived,false),"derived Titan projection base only");
 check(derived.groupCount==2&&derived.playerKeyGroup==playerKey,"derived roster retains base membership and excludes all32 authored groups");
 for(const auto& registry:titan::kRegistries) {
  bool present{};
  for(std::size_t g=0;g<creator.groupCount;++g)if(creator.groups[g].key==registry.key){present=true;
   check(creator.groups[g].slotTypes.size()==registry.slots.size(),"authored registry retains exact declaration width");
   for(std::size_t s=0;s<registry.slots.size();++s)check(creator.groups[g].slotIndices[s]==registry.slots[s].index
       &&creator.groups[g].slotFlags[s]==registry.slots[s].flags(),"authored indices and native schema flags retained");}
  check(present,"every Titan registry is declared in creator projection");
  bool bubbled{};for(const auto& block:creator.bubbleSubBlocks)if(block.bubble==registry.bubble)
   for(const auto key:block.keys)bubbled|=key==registry.key;
  check(bubbled,"every authored group belongs to its exact native bubble");
 }
 auto invalid=layout;invalid.bubbleHashes[2]^=1;
 check(!titan::roster::compose(invalid,groups,*derivedStorage,derived,true)&&derived.groupCount==0,"incompatible native bubble hash rejects publication");
 titan::Runtime runtime;const titan::RuntimeBinding identity{100,7,true};
 check(!runtime.update(identity,16,false).enabled,"no initial source bodies before native arrival");
 const auto first=runtime.update(identity,16,true);unsigned initial{};
 namespace sloane=titan::sloane_presentation;
 const auto* nativeVendor=[](){for(const auto& source:titan_reference::state::activity::vendors::kSources)
  if(source.scenario==titan::kDestination.scenario)return &source;return static_cast<const titan_reference::state::activity::vendors::Source*>(nullptr);}();
 check(nativeVendor&&nativeVendor->key==sloane::kRegistry&&nativeVendor->source==sloane::kSource
     &&nativeVendor->rule==sloane::kRule&&nativeVendor->idle==sloane::kProgram&&nativeVendor->member==sloane::kMember
     &&nativeVendor->sequence==sloane::kIdle&&nativeVendor->position==sloane::kPosition,
     "Sloane source/rule/program/member/sequence/position exactly match production vendor tuple");
 const auto& nativeSource=titan_reference::state::activity::vendors::kAssets[2];
 const auto& nativeProgram=titan_reference::state::activity::vendors::kAssets[3];
 check(nativeSource.asset.definition==sloane::kSourceDescriptor&&nativeSource.offset==sloane::kSourceDefinitionOffset
     &&nativeProgram.asset.definition==sloane::kIdleDescriptor&&nativeProgram.offset==sloane::kProgramDefinitionOffset,
     "Sloane exact native definition offsets match production descriptor bindings");
 const auto& vendorRegistry=titan::kRegistries[29];
 check(vendorRegistry.key==sloane::kRegistry&&vendorRegistry.bubble==2,"Sloane owns native initial bubble");
 check(vendorRegistry.slots[0].descriptorTag==sloane::kSourceDescriptor&&vendorRegistry.slots[2].descriptorTag==sloane::kIdleDescriptor
     &&vendorRegistry.slots[2].type==42&&vendorRegistry.slots[2].flags()==2,"Sloane exact installed descriptors and program auth flag");
 for(bool approached:{false,true}) {
  std::array<std::byte,85> actual{},expected{};bits::Writer a(actual),e(expected);
  check(sloane::write_body(a,first,sloane::kRegistry,42,sloane::kProgram,approached),"Sloane native idle and greeting body");
  namespace animation=titan_reference::middleware::bap::activity_message::native::npc_animation;
  const std::array events{animation::Event{sloane::kIdle,sloane::kGreeting}};
  check(animation::write(e,{sloane::kIdle,animation::kEmptyHash,1},std::span(events).first(approached?1:0)),"production native Sloane performance codec");
  check(actual==expected&&a.bit_count()==e.bit_count()&&a.bit_count()==(approached?165U:101U),"Sloane performance exactly matches production bytes");
 }
 check(sloane::nearby(sloane::kPosition,false),"Sloane position qualifies approach");
 auto position=sloane::kPosition;position[0]+=7;
 check(!sloane::nearby(position,false)&&sloane::nearby(position,true),"native Sloane approach hysteresis");
 position=sloane::kPosition;position[2]+=4;check(!sloane::nearby(position,true),"other native vertical layer cannot greet Sloane");
 check(sloane::body_bits(first,sloane::kRegistry,2,1)==0&&sloane::body_bits(first,sloane::kRegistry,42,3)==0,
     "no named-member duplicate and no mistaken rule-slot performance");
 {
  const auto index=titan::source_index(sloane::kRegistry,sloane::kSource);std::array<std::byte,85> actual{};bits::Writer writer(actual);
  check(sloane::write_body(writer,first,sloane::kRegistry,1,sloane::kSource),"Sloane single native source request");
  std::array<std::byte,85> expected{};bits::Writer reference(expected);
  golden::Source source{sloane::kRegistry,first.sources[index].generation,sloane::kRule,1};
  check(golden::write_source(reference,source),"production vendor source rule override");
  check(actual==expected&&writer.bit_count()==reference.bit_count()&&first.sources[index].first==1,
      "dedicated Sloane source retains single request with exact production vendor rule");
 }
 for(std::uint32_t bubble=0;bubble<64;++bubble) {
  unsigned registries{},sources{};
  for(const auto& registry:titan::kRegistries)registries+=registry.bubble==bubble;
  for(const auto& population:titan::kPopulations)sources+=titan::kRegistries[population.registry].bubble==bubble;
  if(registries)std::printf("Titan zone bubble=%u region=%u hash=%08X state=%02X registries=%u sources=%u\n",
      bubble,bubble*8,layout.bubbleHashes[bubble],unsigned(layout.bubbleStates[bubble]),registries,sources);
 }
 for(const auto& source:first.sources)initial+=source.generation!=0;
 check(initial>0&&initial<93,"arrival activates only native initial bubble");
 const auto prewarm=runtime.update(identity,16,false,40);
 check(prewarm.region==16&&prewarm.enabled,"prefetch cannot replace held native region");
 for(const auto& binding:titan::kPopulations)if(binding.kind==titan::PopulationKind::npc&&titan::kRegistries[binding.registry].bubble==5)
  check(prewarm.sources[titan::source_index(titan::kRegistries[binding.registry].key,binding.source)].generation==0,"prefetch excludes NPC");
 for(const auto& registry:titan::kRegistries)(void)runtime.update(identity,registry.bubble*8U,true);
 const auto all=runtime.update(identity,16,true);unsigned twoCategories{};
 for(std::size_t i=0;i<titan::kPopulations.size();++i){const auto& binding=titan::kPopulations[i];const auto& source=all.sources[i];
  bool rule=!binding.hasRule,objective=binding.tacticalRow<0;
  for(const auto& slot:titan::kRegistries[binding.registry].slots){
   rule|=slot.index==binding.rule&&slot.type==66;
   objective|=slot.index==binding.tactical&&slot.type==3;
  }
  check(rule&&objective,"exact source rule and tactical objective dependencies declared");
  check(binding.categories==1||binding.categories==2,"reviewed one/two category native source policy");
  check(source.generation==1,"visited source retains original native generation");
  const auto actual=body(all,binding);Body expected{};bits::Writer writer(expected.bytes);
  golden::Source reference{titan::kRegistries[binding.registry].key,source.generation,binding.rule,source.first};
  reference.hasRule=binding.hasRule;reference.hasSecondCategory=binding.categories==2;reference.secondRequested=source.second;
  if(binding.tacticalRow>=0)reference.tactical={reference.registry,binding.tactical,binding.tacticalRow,source.generation};
  check(golden::write_source(writer,reference),"production native codec accepts exact Titan authority");expected.count=writer.bit_count();
  check(actual==expected,"all 93 source bodies byte-identical to production native codec");
  check(actual.count==(binding.categories==2?673U:641U),"native category schema width");twoCategories+=binding.categories==2;
 }
 for(unsigned crossing=0;crossing<30;++crossing){(void)runtime.update(identity,40,true);const auto returned=runtime.update(identity,16,true);
  for(std::size_t i=0;i<titan::kPopulations.size();++i)check(body(returned,titan::kPopulations[i])==body(all,titan::kPopulations[i]),"zone return never replays a new source epoch");}
 check(runtime.update(identity,17,true).region==16,"misaligned native report retains held region");
 check(!runtime.update({100,7,false},16,true).enabled&&runtime.snapshot(identity).visited==all.visited,"derived public binding cannot mutate creator source lifetime");
 bool adapted{};
 for(std::size_t i=0;i<titan::kPopulations.size();++i)if(titan::kPopulations[i].tacticalRows>1) {
  const auto& binding=titan::kPopulations[i];const auto key=titan::kRegistries[binding.registry].key;
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
 check(adapted,"exercise actual Titan objective with multiple authored rows");
 check(runtime.update({100,8,true},40,true).visited==(std::uint64_t{1}<<5),"new committed incarnation clears previous source lifetime");
 std::printf("PASS Titan real cache v%u registries=32 sources=93 slots=%u base_groups=%u cached_authored=%u fallback_needed=%u initial_sources=%u two_categories=%u\n",h.version,slots,baseGroups,matched,missing,initial,twoCategories);
}
