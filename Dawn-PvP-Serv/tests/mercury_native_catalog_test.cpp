#include <fstream>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include "state/build_data/inventory/buckets/definition.h"
#include "state/build_data/socket_entry_lists/definition.h"
#include "state/build_data/cache/records/format.h"
#include "state/activity/mercury/runtime.h"
#include "state/activity/mercury/roster.h"
#include "state/activity/mercury/vance_presentation.h"
#include "middleware/encoding/bit_writer.h"
#define dawn mercury_reference
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/coo/native_combatant_authority.h"
#include "../../reference/production/Dawn-production/Dawn/src/middleware/bap/activity_message/native/native_npc_animation_authority.h"
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/vendors/catalog.h"
#include "../../reference/production/Dawn-production/Dawn/src/state/activity/coo/mercury_registries.h"
#undef dawn
#include "state/activity/mercury/lost_sector_inventory.h"
#include "state/activity/mercury/members.h"
namespace records=dawn::state::build_data::cache::records;
namespace layouts=dawn::state::build_data::scenarios;
namespace mercury=dawn::state::activity::mercury;
namespace native=dawn::state::activity::coo::native_combatant;
namespace golden=mercury_reference::state::activity::coo::native_combatant;
namespace bits=dawn::middleware::encoding::bits;
static std::vector<layouts::RosterGroup> groups;
static void check(bool value,const char* label){if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
struct Body {std::array<std::byte,85> bytes{};std::size_t count{};bool operator==(const Body&)const=default;};
Body body(const mercury::Frame& frame,const mercury::PopulationBinding& binding){Body b{};bits::Writer w(b.bytes);check(mercury::write_body(w,frame,mercury::kRegistries[binding.registry].key,1,binding.source),"write exact native Mercury source");b.count=w.bit_count();return b;}
template<class T> void read(std::ifstream& file,T& value) {file.read(reinterpret_cast<char*>(&value),sizeof value);check(bool(file),"read actual cached record");}
template<class T> void skip(std::ifstream& file,std::uint32_t count) {file.seekg(static_cast<std::streamoff>(sizeof(T)*std::uint64_t(count)),std::ios::cur);}
int main(int argc,char** argv) {
 check(argc==2,"cache path argument");std::ifstream file(argv[1],std::ios::binary);check(bool(file),"open local cache read only");
 records::Prefix prefix{};read(file,prefix);check(prefix.magic==records::kCacheMagic&&prefix.version==records::kCacheFormatVersion,"cache version before current header offsets");
 file.seekg(0);records::Header h{};read(file,h);
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
  if(std::string_view(row.name.data(),row.nameLength)!=mercury::kDestination.activity) continue;
  found=true;layout.bubbleCount=row.bubbleCount;layout.bubbleStates=row.bubbleStates;layout.bubbleHashes=row.bubbleHashes;layout.tag=row.tag;layout.rosterGroupCount=row.rosterGroupCount;layout.rosterGroups=row.rosterGroups;
  layout.bubbleGroupCount=row.bubbleGroupCount;layout.bubbleGroups=row.bubbleGroups;
  for(std::size_t g=0;g<row.bubbleGroupMasks.size();++g) for(std::size_t b=0;b<8;++b)
   layout.bubbleGroupMasks[g]|=std::uint64_t(row.bubbleGroupMasks[g][b])<<(b*8);
 }
 check(found && layout.tag==mercury::kDestination.scenario,"real Mercury cached layout");
 groups.resize(h.rosterGroupCount);
 for(auto& group:groups) {records::RosterGroupRecord row{};read(file,row);group.registryKey=row.registryKey;group.objectTag=row.objectTag;
 group.slotCount=row.slotCount;group.slotTypes=row.slotTypes;group.slotFlags=row.slotFlags;group.slotIndices=row.slotIndices;}

 unsigned patrols{},vendors{},events{},unselected{};
 for(std::size_t i=0;i<mercury::kRegistries.size();++i) {
  const auto& own=mercury::kRegistries[i];const auto& ref=mercury_reference::state::activity::coo::mercury::kRegistries[i];
  check(own.key==ref.key&&own.objectTag==ref.objectTag&&own.bubbleHash==ref.bubbleHash&&own.slots.size()==ref.slots.size(),"exact selected registry provenance");
  check(own.bubble<layout.bubbleCount&&layout.bubbleHashes[own.bubble]==own.bubbleHash,"real Mercury bubble hash");
  for(std::size_t j=0;j<own.slots.size();++j) {
   const auto& x=own.slots[j];const auto& y=ref.slots[j];
   check(x.index==y.index&&x.type==y.type&&x.authSchema==y.authSchema&&x.descriptorTag==y.descriptorTag&&x.componentClass==y.componentClass&&x.senseSchema==y.senseSchema,"exact source/portal/vendor slot identity");
   if(x.type==1&&mercury::source_index(own.key,x.index)==mercury::kPopulations.size())++unselected;
  }
 }
 auto storage=std::make_unique<mercury::roster::Storage>();mercury::roster::wire::Roster creator{},derived{};
 check(mercury::roster::compose(layout,groups,*storage,creator,true),"actual cache creator roster");
 auto other=std::make_unique<mercury::roster::Storage>();
 check(mercury::roster::compose(layout,groups,*other,derived,false),"actual cache derived roster");
 check(creator.playerKeyGroup&&creator.playerKeyGroup==derived.playerKeyGroup,"player membership survives");
 mercury::Runtime runtime;mercury::RuntimeBinding identity{100,7,true};
 check(!runtime.update(identity,120,false).enabled,"no bodies before arrival");
 const auto first=runtime.update(identity,120,true);
 for(std::size_t i=0;i<mercury::kPopulations.size();++i) {
  const auto& binding=mercury::kPopulations[i];const auto& registry=mercury::kSourceRegistries[binding.registry];
  bool declared{},rule{},tactical{};
  for(const auto& slot:registry.slots){declared|=slot.type==1&&slot.index==binding.source;rule|=slot.type==66&&slot.index==binding.rule;tactical|=slot.type==3&&slot.index==binding.tactical;}
  check(declared&&(!binding.hasRule||(rule&&tactical)),"every policy source/rule/tactical declared");
  const auto* proof=mercury::members::source(registry.key,binding.source);check(proof&&proof->categories==binding.categories&&proof->digest.size()==64,"exact pinned native source category and digest");
  bool descriptor{};for(const auto& slot:registry.slots)descriptor|=slot.index==binding.source&&slot.descriptorTag==proof->descriptor;check(descriptor,"member source descriptor exact");
  for(std::size_t j=0;j<proof->count;++j){const auto& choice=mercury::members::kChoices[proof->first+j];check(choice.weight&&choice.category<proof->categories&&mercury::members::find(registry.key,binding.source,choice.offset)==&choice,"native member lookup unique source-local offset");}
  if(binding.kind==mercury::PopulationKind::event){++events;check(!first.sources[i].generation&&!mercury::body_bits(first,registry.key,1,binding.source),"staged event remains dormant");continue;}
  if(binding.kind==mercury::PopulationKind::npc)++vendors;else ++patrols;
  check(first.sources[i].generation==1,"persistent initial generation");
  auto actual=body(first,binding);Body expected{};bits::Writer writer(expected.bytes);
  golden::Source source{registry.key,1,binding.rule,first.sources[i].first};source.hasRule=binding.hasRule;
  source.hasSecondCategory=binding.categories==2;source.secondRequested=0;
  if(binding.tacticalRow>=0)source.tactical={registry.key,binding.tactical,binding.tacticalRow,1};
  check(golden::write_source(writer,source),"gold native source encoder");expected.count=writer.bit_count();check(actual==expected,"native body matches production codec");
 }
 check(patrols==27&&vendors==1&&events==4,"explicit patrol vendor event classification");
 const auto repeat=runtime.update(identity,120,true);check(repeat.sources[0].generation==first.sources[0].generation&&repeat.sources[0].first==first.sources[0].first,"rearrival does not duplicate or reset requests");
 check(!runtime.update({100,7,false},120,true).enabled,"derived host cannot author populations");
 check(!runtime.snapshot({100,8,true}).enabled,"stale incarnation cannot read source authority");
 namespace vance=mercury::vance_presentation;
 const auto* vendor=[](){for(const auto& v:mercury_reference::state::activity::vendors::kSources)if(v.scenario==mercury::kDestination.scenario)return &v;return static_cast<const mercury_reference::state::activity::vendors::Source*>(nullptr);}();
 check(vendor&&vendor->key==vance::kRegistry&&vendor->rule==vance::kRule&&vendor->idle==vance::kProgram&&vendor->sequence==vance::kIdle&&vendor->position==vance::kPosition,"exact Vance presentation tuple");
 check(vance::body_bits(first,vance::kRegistry,42,2)==101&&vance::body_bits(first,vance::kRegistry,42,2,true)==165,"Vance native idle and greeting body widths");
 for(bool nearby:{false,true}){Body actual{},expected{};bits::Writer a(actual.bytes),b(expected.bytes);check(vance::write_body(a,first,vance::kRegistry,42,2,nearby),"Vance body");
 namespace animation=mercury_reference::middleware::bap::activity_message::native::npc_animation;const std::array event{animation::Event{vance::kIdle,vance::kGreeting}};
 check(animation::write(b,animation::Control{vance::kIdle,0x811C9DC5U,1},nearby?std::span<const animation::Event>{event}:std::span<const animation::Event>{}),"Vance native golden animation");actual.count=a.bit_count();expected.count=b.bit_count();check(actual==expected,"Vance exact native bytes");}
 namespace sector=mercury::lost_sector_inventory;
 check(sector::kSources.size()==22&&sector::kStages.size()==8&&sector::kRegistries.size()==2&&sector::kRewardRegistries.size()==1,"separate22 source lost sector inventory");
 for(const auto& registry:sector::kRegistries)check(layout.bubbleHashes[registry.bubble]==registry.bubbleHash,"real Pariah bubble provenance");
 for(const auto& binding:sector::kSources){const auto& registry=sector::kRegistries[binding.registry];const auto* proof=mercury::members::source(registry.key,binding.source);check(proof&&proof->categories==binding.categories,"lost sector has separate exact native member/category pins");}
 std::printf("PASS Mercury:27 patrols1 vendor4 dormant events20 selected registries; %u unselected alternatives; creator%zu derived%zu groups\n",unselected,creator.groupCount,derived.groupCount);
}
