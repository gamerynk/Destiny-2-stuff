#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../dawn/src/core/ui/hud/overlays/ui_hud_status_overlay.cpp"
namespace fixture {
bool world{};
std::uint64_t session{};
unsigned checks{},resets{},metadata{};
dawn::client::player::position::Snapshot player{{1,2,3},true};
std::vector<std::string> text;
void require(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
}
namespace dawn::client::hooks::bootflow { bool in_world() noexcept{return fixture::world;} }
namespace dawn::client::player::position {
void reset() noexcept{++fixture::resets;fixture::player={};}
Snapshot snapshot() noexcept{return fixture::player;}
}
namespace dawn::state::activity::membership {
std::uint64_t live_region_session(std::uint64_t fallback) noexcept{return fixture::session?fixture::session:fallback;}
std::int32_t reported_region(std::uint64_t) noexcept{++fixture::metadata;return 3;}
}
namespace dawn::state::activity::destination {
bool snapshot(std::uint64_t,DestinationSelection& out) noexcept {
    ++fixture::metadata;out={};const char name[]="fixture_map";
    std::memcpy(out.packageName.data(),name,sizeof(name)-1);out.packageNameLength=sizeof(name)-1;return true;
}
}
namespace dawn::state::build_data {
bool find_hash_name(std::uint32_t,hash_names::Name&) noexcept{++fixture::metadata;return false;}
bool find_scenario_layout(std::string_view,scenarios::Definition&) noexcept{++fixture::metadata;return false;}
bool find_nearest_spawn_point(std::string_view,const std::array<float,spawn_sets::kPositionComponents>&,
    spawn_sets::Point&,float&) noexcept{++fixture::metadata;return false;}
}
namespace dawn::core::ui::hud {
bool enabled(StatusLine) noexcept{return true;}
const char* display_name(StatusLine) noexcept{return "fixture label";}
}
ImGuiStyle::ImGuiStyle(){std::memset(this,0,sizeof(*this));}
namespace ImGui {
ImGuiStyle& GetStyle(){static ImGuiStyle style;return style;}
ImVec2 CalcTextSize(const char*,const char*,bool,float){return {10,10};}
void SameLine(float,float){}
void TextDisabled(const char* format,...) {fixture::text.emplace_back(format);}
void TextUnformatted(const char* text,const char*){fixture::text.emplace_back(text);}
}
int main(){
    namespace status=dawn::core::ui::hud::overlays::status;
    fixture::world=true;fixture::session=0;
    const auto missing=status::read_status();
    fixture::require(missing.inWorld,"native world state remains true without local server membership");
    fixture::require(fixture::resets==0&&fixture::player.present,"missing external metadata does not erase position");
    fixture::require(fixture::metadata==0,"missing session never fabricates or queries destination metadata");
    fixture::require(std::strcmp(missing.activity.data(),"unknown")==0
        &&std::strcmp(missing.bubble.data(),"unknown")==0
        &&std::strcmp(missing.sliceSet.data(),"unknown")==0
        &&std::strcmp(missing.spawn.data(),"unknown")==0,"all unavailable fields are explicit unknown values");
    status::draw();
    unsigned unknown{};for(const auto& text:fixture::text)if(text=="unknown")++unknown;
    fixture::require(unknown==4&&fixture::resets==0,"actual draw path renders four unknown metadata rows without resetting position");
    fixture::session=7;
    const auto known=status::read_status();
    fixture::require(known.inWorld&&std::strcmp(known.activity.data(),"fixture_map")==0,
        "existing session metadata path remains functional");
    fixture::require(fixture::metadata==3&&fixture::resets==0,"existing destination/region/layout reads preserve position");
    fixture::world=false;
    fixture::require(!status::read_status().inWorld&&fixture::resets==1&&!fixture::player.present,
        "actual native world exit clears position even when old session remains");
    std::printf("PASS %u actual HUD world presence checks; no game access\n",fixture::checks);
}
