#include "state/activity/crucible/menu_rules.h"
#include "state/activity/crucible/match_host.h"
#include "middleware/bap/activity_host_manager/request/selection/internal.h"
#include "middleware/encoding/bit_writer.h"
#include <cstdio>
#include <cstdlib>

namespace menu=dawn::state::activity::crucible::menu_rules;
namespace host=dawn::state::activity::crucible::host;
namespace selection=dawn::middleware::bap::activity_host_manager::request::selection;
namespace bits=dawn::middleware::encoding::bits;
void check(bool v,const char* label) { if(!v) {std::printf("FAIL: %s\n",label);std::exit(1);} }
int main() {
    std::array<std::byte,232> body{};bits::Writer w(body);
    const auto write=[&](std::uint64_t value,std::uint8_t width) {check(w.write(value,width),"encode descriptor");};
    write(1,4);write(503,12);write(457,12);
    write(0,1);write(0,1);write(0,1);write(0,5);write(0,8);
    write(0,1);write(0,1);write(0,1);write(0,1);write(0,1);write(0,1);write(0,1);
    write(1,1);write(3,6);
    // captured options: time choice 3, score choice 1, respawn choice 1
    for (const auto& p:std::array<menu::Selection,3>{{{65,3},{64,1},{21,1}}}) {
        write(p.index+1,9);write(p.choice+1,4);
    }
    const auto n=w.bit_count();bits::Reader reader(body,n);selection::ActivityManagerSelection parsed{};
    check(selection::parse(reader,parsed)&&reader.remaining_bits()==0,"decode complete native menu selection");
    check(parsed.menuOptionCount==3&&parsed.menuOptions[0].index==65&&parsed.menuOptions[0].choice==3,
          "native biased option/choice encoding");
    for(std::size_t length=0;length<n;++length) {
        bits::Reader shortReader(body,length);selection::ActivityManagerSelection shortResult{};
        check(!selection::parse(shortReader,shortResult),"truncated menu descriptor refused");
    }
    std::array<menu::Selection,3> options{};
    for(std::size_t i=0;i<options.size();++i)options[i]={parsed.menuOptions[i].index,parsed.menuOptions[i].choice};
    menu::Rules rules{};
    check(menu::resolve(options,rules)&&rules.scoreLimit==25&&rules.durationMs==900'000,
          "captured menu means 25 points and 15 minutes");
    const std::array<std::uint32_t,6> limits{10,25,50,75,100,125};
    const std::array<std::uint64_t,6> times{300'000,480'000,600'000,900'000,1'200'000,1'800'000};
    for(std::int16_t score=0;score<6;++score) for(std::int16_t time=0;time<6;++time) {
        options[0]={65,time};options[1]={64,score};
        check(menu::resolve(options,rules)&&rules.scoreLimit==limits[score]&&rules.durationMs==times[time],
              "every supported Rumble score/time combination");
        host::Match m;check(m.configure({1,2,3},host::rules::Mode::rumble,rules.scoreLimit,rules.durationMs),"configure selected rules");
        const std::array<host::Member,2> members{{{1,101,0,true},{2,102,1,true}}};
        check(m.roster(members,2,1000),"start match");
        m.tick(1000+rules.durationMs-1);check(m.view().phase==host::Phase::playing,"selected duration does not end early");
        m.tick(1000+rules.durationMs);check(m.view().phase==host::Phase::results,"selected duration ends exactly");
    }
    for (auto limit: {25U,50U}) {
        host::Match m;check(m.configure({1,2,3},host::rules::Mode::rumble,limit,900'000),"score match configured");
        const std::array<host::Member,2> members{{{1,101,0,true},{2,102,1,true}}};
        check(m.roster(members,2,1000),"score match started");
        for(std::uint32_t score=1;score<=limit;++score) {
            check(m.kill(1,102,score,score,1000+score),"unique kill accepted");
            check(m.view().phase==(score==limit?host::Phase::results:host::Phase::playing),"ends at selected score, including beyond 20");
        }
    }
    options={{{65,3},{64,6},{21,1}}};check(!menu::resolve(options,rules),"invalid choice refused");
    options={{{65,3},{64,1},{64,2}}};check(!menu::resolve(options,rules),"duplicate choices refused");
    options={{{65,3},{64,1},{22,2}}};check(!menu::resolve(options,rules),"conflicting score options refused");
    check(!menu::resolve(std::span(options).first(1),rules),"missing score never replaced with 20");
    check(!menu::resolve({},rules),"missing choices never replaced with defaults");
    std::puts("PASS: native descriptor, all 36 menu combinations, 25/50 score endings, selected timers, malformed choices");
}
