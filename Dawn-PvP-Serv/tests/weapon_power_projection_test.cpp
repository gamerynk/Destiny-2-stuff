#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include "middleware/datagen/character_record/appearance/internal.h"
#include "state/build_data/runtime.h"
#include "state/equipment/light/definition.h"
#include "core/logging/log.h"
namespace ap=dawn::middleware::datagen::character_record::appearance;
namespace lo=dawn::middleware::datagen::family4::loadout;
namespace layout=dawn::middleware::datagen::character_record::layout;
namespace constants=dawn::state::build_data::constants;
static constants::InvestmentConstants named{true,0,13,{4,3,5,7,6,8}};
static std::array<ap::details::Definition,4> definitions{};
static unsigned checks{};
static void check(bool ok){++checks;if(!ok){std::printf("FAIL check%u\n",checks);std::exit(1);}}
namespace dawn::state::build_data {
bool find_investment_constants(constants::InvestmentConstants& out) noexcept {out=named;return true;}
bool find_configured_item_detail(std::uint16_t index,items::details::Definition& out) noexcept {if(index>=definitions.size())return false;out=definitions[index];return true;}
}
namespace dawn::core::log {void write(Channel,Level,std::string_view) noexcept {}}
namespace dawn::middleware::datagen::character_record::appearance {
bool resolve_equipped(const lo::SlottedInstance& source,details::Definition& detail,Equipped& out) noexcept {
 if(!dawn::state::build_data::find_configured_item_detail(source.instance.baseDefinitionIndex,detail))return false;
 out={};out.definitionIndex=source.instance.baseDefinitionIndex;out.equipmentSlot=source.equipmentSlot;
 out.plugs.fill(details::kUnavailableItemIndex);out.laneCount=1;out.plugs[0]=3;return true;
}
}
static int stat(const std::array<layout::StatRow,layout::kStatRowCapacity>& table,int key){for(const auto& row:table)if(row.value>0&&row.key==key)return row.value;return -1;}
int main(){
 auto instances=std::make_unique<lo::ResolvedInstances>();instances->itemCount=3;
 for(unsigned i=0;i<3;++i){instances->items[i].equipmentSlot=static_cast<std::uint8_t>(7+i);instances->items[i].instance.baseDefinitionIndex=static_cast<std::uint16_t>(i);instances->items[i].instance.level=static_cast<int>(106-i);definitions[i].statCount=2;definitions[i].stats[0]={13,99};definitions[i].stats[1]={32,20};}
 definitions[3].statCount=2;definitions[3].stats[0]={13,77};definitions[3].stats[1]={32,2};
 layout::Appearance output{};check(ap::apply_stats(*instances,1060,output));check(stat(output.characterStats,0)==1060);
 for(unsigned i=0;i<3;++i){check(stat(output.weaponStats[i],13)==1060-static_cast<int>(i)*10);check(stat(output.weaponStats[i],32)==22);}
 instances->items[0].instance.level=1;check(ap::apply_stats(*instances,1060,output));check(stat(output.weaponStats[0],13)==750);
 instances->items[0].instance.level=-1;check(!ap::apply_stats(*instances,1060,output));
 instances->items[0].instance.level=(std::numeric_limits<int>::max)();check(!ap::apply_stats(*instances,1060,output));
 instances->items[0].instance.level=106;named.weaponPowerStatRow=64;check(!ap::apply_stats(*instances,1060,output));
 named.weaponPowerStatRow=13;definitions[0].stats[1].row=64;check(!ap::apply_stats(*instances,1060,output));
 int value=123;check(!dawn::state::equipment::light::item_power(-1,value));check(value==123);check(dawn::state::equipment::light::item_power(0,value));check(value==0);
 std::printf("PASS native weapon Power projection checks=%u\n",checks);
}
