#include "client/crucible/native_match_projection.h"
#include "state/activity/crucible/menu_rules.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace p = dawn::client::crucible::projection;
namespace c = dawn::state::activity::crucible::completion;
void check(bool value, const char* reason) {
    if (!value) { std::printf("FAIL: %s\n", reason); std::exit(1); }
}
int main(int argc, char** argv) {
    p::host::View v{}; v.key={10,20,30};v.phase=p::host::Phase::playing;
    v.mode=dawn::state::activity::crucible::match_rules::Mode::rumble;
    v.remainingMs=420000;v.count=3;v.scoreLimit=20;
    v.rows[0]={1,101,6,2,6,0,{}};v.rows[1]={2,102,2,3,2,5,{}};
    v.rows[2]={3,103,1,4,1,11,{}};v.scores[0]=6;v.scores[5]=2;v.scores[11]=1;
    const auto bits=[](float value) { std::uint32_t result{};std::memcpy(&result,&value,4);return result; };
    p::HudScores bars{};
    check(p::hud_scores(v,101,bars)&&bars.local==6&&bars.opponent==2,
        "bar scores select local player and best opponent independent of faction metadata");
    check(p::hud_scores(v,103,bars)&&bars.local==1&&bars.opponent==6,
        "trailing player's red bar uses the leader");
    check(!p::hud_scores(v,999,bars),"bar scores reject a foreign local identity");
    auto solo=v;solo.count=1;
    check(p::hud_scores(solo,101,bars)&&bars.local==6&&bars.opponent==0,"solo keeps a zero opponent bar");
    auto invalid=v;invalid.rows[1].side=12;
    check(!p::hud_scores(invalid,101,bars),"malformed bar roster rejected");
    auto ranked=v;ranked.count=8;
    for (std::size_t i=0;i<8;++i) {
        ranked.rows[i].player=100+i;ranked.rows[i].score=static_cast<std::int32_t>(8-i);
    }
    for (std::size_t i=0;i<8;++i) {
        std::size_t label=99;
        check(p::place_label(ranked,100+i,label)&&label==i,"all eight authored positions follow score order");
    }
    ranked.rows[7].score=ranked.rows[6].score;
    std::size_t tiedSeventh=99;
    check(p::place_label(ranked,107,tiedSeventh)&&tiedSeventh==14,"tied seventh uses native tied variant");
    ranked.rows[7].player=ranked.rows[6].player;
    check(!p::place_label(ranked,100,tiedSeventh),"duplicate placement roster refused");
    p::Countdown clock;std::uint64_t remaining{};
    check(clock.sample(v,1000,1000,3,remaining)&&remaining==420000,"live seven-minute clock");
    check(clock.sample(v,1000,2000,3,remaining)&&remaining==419000,"clock advances between polls");
    check(!clock.sample(v,1000,11001,3,remaining),"stale HTTP state cannot override native timer");
    check(!clock.sample(v,1000,999,3,remaining),"future receipt rejected");
    check(clock.sample(v,2000,2000,6,remaining)&&remaining==5000,"end starts at five even before next HTTP result");
    v.phase=p::host::Phase::results;
    check(clock.sample(v,6000,6000,6,remaining)&&remaining==1000,"result polls cannot restart end timer");
    check(clock.sample(v,6000,7000,6,remaining)&&remaining==0,"end clock reaches zero at five seconds");
    check(clock.sample(v,7000,7000,7,remaining)&&remaining==30000,"results retain thirty-second review interval");
    check(clock.sample(v,36500,37000,7,remaining)&&remaining==0,"orbit countdown expires");
    check(!clock.sample(v,37000,37000,8,remaining),"return phase leaves native timer alone");
    ++v.key.generation;
    check(clock.sample(v,38000,38000,6,remaining)&&remaining==5000,"next match resets countdown");
    check(c::phase(104999,100000)==6&&c::phase(105000,100000)==7,"server end transition exactly at five seconds");
    check(c::phase(134999,100000)==7&&c::phase(135000,100000)==8,"server orbit transition at end of results");
    check(c::phase(99000,100000)==6,"backward time does not prematurely end match");

    std::array<std::byte,p::kModelSize> model{};
    p::write(std::span(model),0,std::uint32_t{3});
    for (std::size_t i=0;i<3;++i) {
        p::write(std::span(model),i*p::kRowStride+0x50,v.rows[2-i].player);
        p::write(std::span(model),i*p::kRowStride+0x18,std::uint64_t{1234+i});
        p::write(std::span(model),i*p::kRowStride+0x60,std::array<char,8>{'N','a','t','i','v','e','!','\0'});
    }
    p::Columns columns{};
    for (std::size_t i=0;i<3;++i) columns[i]={0x80B9E364,p::kStatKeys[i],0x80B9E364,1,0x80B9E364,2,0,{}};
    const auto before=model;
    check(p::apply(model,v,101,columns),"native projection accepts matching roster");
    p::PlaceLabels placeLabels{};
    for (std::size_t i=0;i<placeLabels.size();++i) placeLabels[i]={0x80B9E378,p::kPlaceKeys[i]};
    auto live=v;live.phase=p::host::Phase::playing;
    p::write(std::span(model),0x2114,std::uint8_t{3});
    check(p::apply_place(model,live,101,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[0],"native first place");
    const auto firstToken=p::read<std::uint64_t>(model,0x2078);
    check(p::apply_place(model,live,102,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[1]
        &&p::read<std::uint64_t>(model,0x2078)!=firstToken,"rank changes invalidate native text cache");
    check(p::apply_place(model,live,103,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[2],"native third place");
    live.rows[1].score=6;
    check(p::apply_place(model,live,101,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[8],"native tied first place");
    check(p::apply_place(model,live,103,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[2],"two tied leaders leave trailing player third");
    live.rows[0].score=10;live.rows[1].score=1;
    check(p::apply_place(model,live,103,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[9],"native tied second place");
    check(p::read<std::uint32_t>(model,0x2088)==0,"place text drops obsolete Round 0 argument");
    live.count=1;
    check(p::apply_place(model,live,101,placeLabels)
        &&p::read<p::Headline>(model,0x2080).key==p::kPlaceKeys[0],"solo native first place");
    const auto headlineBefore=model;
    live.phase=p::host::Phase::results;
    check(!p::apply_place(model,live,101,placeLabels)&&model==headlineBefore,"results preserve native match-complete label");
    live.phase=p::host::Phase::playing;live.mode=dawn::state::activity::crucible::match_rules::Mode::clash;
    check(!p::apply_place(model,live,101,placeLabels)&&model==headlineBefore,"Clash keeps native mode headline");
    live.mode=dawn::state::activity::crucible::match_rules::Mode::rumble;
    placeLabels[0].key=0;
    check(!p::apply_place(model,live,101,placeLabels)&&model==headlineBefore,"unverified string definitions cause no writes");
    check(p::read<std::uint32_t>(model,0x2070)==2&&p::read<float>(model,0x2068)==6,"blue uses local player, not first row");
    check(p::read<float>(model,0x14C)==1&&p::read<float>(model,p::kRowStride+0x14C)==2,"remote headline scores populated");
    check(p::read<std::uint32_t>(model,0x1DD4)==3&&p::read<float>(model,0x1DB8)==20,"native results columns and score limit");
    check(p::read<std::array<float,3>>(model,2*p::kRowStride+0xF0)==std::array<float,3>{6,6,2},"local native stat row");
    for (std::size_t i=0;i<3;++i)
        check(std::memcmp(model.data()+i*p::kRowStride+8,before.data()+i*p::kRowStride+8,0xE4)==0,
            "native account, character, key, name and emblem are preserved");
    auto broken=model;p::write(std::span(broken),p::kRowStride+0x50,std::uint64_t{999});
    auto unchanged=broken;
    check(!p::apply(broken,v,101,columns)&&broken==unchanged,"foreign row rejected without partial writes");
    broken=model;p::write(std::span(broken),p::kRowStride+0x50,std::uint64_t{103});unchanged=broken;
    check(!p::apply(broken,v,101,columns)&&broken==unchanged,"duplicate row rejected without partial writes");
    check(!p::apply(model,v,999,columns),"foreign local identity refused");
    check(!p::apply(std::span(model).first(100),v,101,columns),"short model refused");
    auto wrongColumns=columns;wrongColumns[0].key=0;
    check(!p::apply(model,v,101,wrongColumns),"unmatched package stat definitions refused");
    for (auto choice: {std::int16_t{1},std::int16_t{2}}) {
        namespace menu=dawn::state::activity::crucible::menu_rules;
        const std::array<menu::Selection,2> selected{{{64,choice},{65,3}}};menu::Rules selectedRules{};
        check(menu::resolve(selected,selectedRules),"resolve menu for HUD");
        v.scoreLimit=selectedRules.scoreLimit;
        check(p::apply(model,v,101,columns)&&p::read<float>(model,0x1DB8)==(choice==1?25.0F:50.0F),
            "native progress-bar maximum follows 25/50 menu selection");
        check(p::hud_scores(v,101,bars)&&bars.local/p::read<float>(model,0x1DB8)==(choice==1?0.24F:0.12F),
            "six points produce different native fill fractions for selected 25/50 limits");
    }
    v.mode=dawn::state::activity::crucible::match_rules::Mode::clash;
    v.rows[0].side=0;v.rows[1].side=1;v.rows[2].side=0;v.scores[0]=7;v.scores[1]=2;
    check(p::hud_scores(v,103,bars)&&bars.local==7&&bars.opponent==2,"Clash bar scores aggregate teams");
    check(p::apply(model,v,101,columns)&&p::read<std::uint8_t>(model,0x2044)==0
        &&p::read<float>(model,0x2064)==7,"Clash retains team-score presentation");
    if(argc>1) {
        std::ifstream stream(argv[1],std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(stream),{}};
        check(bytes.size()>=p::kModelSize,"read captured native model");
        auto captured=std::span(reinterpret_cast<std::byte*>(bytes.data()),bytes.size());
        check(p::read<std::uint32_t>(captured,0)==3,"capture has three native rows");
        for(std::size_t i=0;i<3;++i)v.rows[i].player=p::read<std::uint64_t>(captured,i*p::kRowStride+0x50);
        check(p::apply(captured,v,v.rows[0].player,columns),"captured native layout accepts native stat projection");
    }
    std::puts("PASS: native HUD identity mapping, results columns, freshness, live/end/orbit timers, exact server deadlines");
}
