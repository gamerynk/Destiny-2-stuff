#include <cstdio>
#include <fstream>
#include "state/activity/entity_retirement/policy.h"
namespace retire=dawn::state::activity::entity_retirement;
static unsigned checks{};
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(){
 retire::Journal<8,4> h{};using Result=decltype(h)::Result;
 retire::Mask lease{},other{};lease[0]=3;other[0]=4;
 CHECK(h.commit_join(h.prepare_join(1,101,lease)));CHECK(h.commit_join(h.prepare_join(2,102,other)));
 const auto incarnation=h.find(1)->joinRevision;
 retire::RequestProvenance proof{9000,0,0}; const auto send=[&](std::uint64_t account,std::uint64_t join,const retire::wire::Abandon& release){++proof.authenticatedOrdinal;++proof.taskId;return h.abandon_current(account,join,release,proof);};
 retire::wire::Abandon release{};release.mask[0]=1;release.reason=3;release.selector=1;
 CHECK(send(1,0,release)==Result::refused);CHECK(send(1,incarnation+1,release)==Result::refused);
 CHECK(send(2,h.find(2)->joinRevision,release)==Result::refused);
 auto malformed=release;malformed.reason=2;CHECK(send(1,incarnation,malformed)==Result::refused);
 malformed=release;malformed.selector=65;CHECK(send(1,incarnation,malformed)==Result::refused);
 malformed=release;malformed.mask={};CHECK(send(1,incarnation,malformed)==Result::refused);
 CHECK(h.serial==0);CHECK(send(1,incarnation,release)==Result::queued);
 CHECK(h.find(1)->joined&&!h.find(1)->departed&&h.find(1)->lease==lease);
 CHECK(h.at(1)&&h.at(1)->mask==release.mask&&h.at(1)->reason==3&&h.at(1)->joinRevision==incarnation);
 CHECK(h.at(1)->request.connectionIncarnation==proof.connectionIncarnation&&h.at(1)->request.authenticatedOrdinal==proof.authenticatedOrdinal);
 CHECK(h.abandon_current(1,incarnation,release)==Result::refused);
 CHECK(h.abandon_current(1,incarnation,release,proof)==Result::refused);
 auto wrongConnection=proof;wrongConnection.connectionIncarnation++;wrongConnection.authenticatedOrdinal+=100;
 CHECK(h.abandon_current(1,incarnation,release,wrongConnection)==Result::refused);
 CHECK(send(1,incarnation,release)==Result::duplicate);
 CHECK(h.delivered(1,0,1));CHECK(h.delivered(2,0,1));
 // a native mask carries no salt, so delivery must not reset duplicate protection
 CHECK(send(1,incarnation,release)==Result::duplicate);CHECK(h.serial==1);
 release.mask[0]=2;CHECK(send(1,incarnation,release)==Result::queued);CHECK(h.serial==2);
 CHECK(h.at(2)->request.taskId==proof.taskId);
 CHECK(h.at(2)&&h.at(2)->mask==release.mask);
 CHECK(h.depart(1,101,lease));CHECK(send(1,incarnation,release)==Result::refused);
 CHECK(h.abandon(1,release)==Result::duplicate);
 CHECK(h.delivered(1,1,2));CHECK(h.delivered(2,1,2));
 CHECK(h.commit_join(h.prepare_join(1,103,lease)));
 CHECK(send(1,incarnation,release)==Result::refused);CHECK(h.serial==2);
 retire::Journal<8,4> cycle{};
 CHECK(cycle.commit_join(cycle.prepare_join(1,101,lease)));
 const auto join=cycle.find(1)->joinRevision;
 retire::RequestProvenance wrap{77,1,UINT32_MAX};release.mask[0]=1;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::queued);
 wrap.authenticatedOrdinal++;wrap.taskId=0;release.mask[0]=2;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::queued);
 wrap.authenticatedOrdinal++;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::refused);
 wrap.taskId=UINT32_MAX;wrap.authenticatedOrdinal++;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::refused);
 wrap.taskId=0x80000000U;wrap.authenticatedOrdinal++;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::refused);
 wrap.taskId=1;wrap.connectionIncarnation++;wrap.authenticatedOrdinal++;
 CHECK(cycle.abandon_current(1,join,release,wrap)==Result::refused);
 std::array<std::byte,retire::wire::kAbandonBytes> captured{};
 std::ifstream capture("tests/fixtures/native_cell_abandon.bin",std::ios::binary);
 CHECK(capture.read(reinterpret_cast<char*>(captured.data()),captured.size()).good());
 retire::wire::Abandon actual{};CHECK(retire::wire::parse_abandon(captured,actual));
 CHECK(actual.selector==1 && actual.reason==3 && actual.mask[1]==0x30000000U && actual.mask[2]==0x1840U);
 unsigned nonzero{};for(auto word:actual.mask)if(word)++nonzero;CHECK(nonzero==2);
 retire::Journal<8,4> real{};CHECK(real.commit_join(real.prepare_join(1,101,actual.mask)));
 CHECK(real.abandon_current(1,real.find(1)->joinRevision,actual,{9,1,42})==Result::queued);
 CHECK(real.at(1)->mask==actual.mask);
 captured.back()|=std::byte{1};CHECK(!retire::wire::parse_abandon(captured,actual));
 std::printf("PASS: %u native joined cell abandonment checks; lease/current view, native operation, replay/reuse fence\n",checks);
}

