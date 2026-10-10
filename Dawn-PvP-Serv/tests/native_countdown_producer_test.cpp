#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "middleware/bap/activity_message/sensor_auth_update.h"
#include "middleware/bap/activity_message/native/activity_clock_authority.h"
#include "middleware/encoding/bit_reader.h"
#include "state/activity/crucible/native_countdown_publication.h"
namespace sa=dawn::middleware::bap::activity_message::sensor_auth_update;
namespace clk=dawn::middleware::bap::activity_message::native::activity_clock;
namespace bits=dawn::middleware::encoding::bits;
void check(bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);}}
std::uint64_t read(bits::Reader& r,std::uint8_t n){std::uint64_t v{};check(r.read(n,v),"native clock bounds");return v;}
int main(){
 for(std::uint64_t ms:{300000ULL,900000ULL,1200000ULL})for(bool running:{false,true}){
  sa::Snapshot s{};s.nativeCountdown=true;s.nativeCountdownRunning=running;
  check(clk::from_milliseconds(ms,s.nativeCountdownDurationTicks),"selected menu limit converts");
  check(clk::from_milliseconds(running?ms:ms-1000,s.nativeCountdownRemainingTicks),"results freezes selected remaining");
  std::array<std::byte,64> out{};bits::Writer w(out);
  check(sa::write_auth_body(w,s,0,18,2,false)&&w.bit_count()==386,"native18 width preserved");
  std::size_t count{};check(w.finish(count),"native18 finish");bits::Reader r(out);
  check(read(r,1)==1&&read(r,64)==0,"native timer active and lower bound");
  check(read(r,64)==s.nativeCountdownDurationTicks&&read(r,64)==0,"exact selected upper and initial elapsed");
  check(read(r,64)==s.nativeCountdownRemainingTicks&&read(r,64)==0,"selected remaining and native epoch0");
  check(read(r,32)==std::bit_cast<std::uint32_t>(running?1.0F:0.0F),"native playing/results rate");
  char file[160]{};std::snprintf(file,sizeof(file),"build/native-pvp-20261002/ui/countdown-%llu-%u.bin",ms,static_cast<unsigned>(running));
  std::ofstream f(file,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(count));
 }
 namespace c=dawn::state::activity::crucible;
 for(std::uint64_t age:{0ULL,1ULL,4999ULL,5000ULL,5001ULL,34999ULL,35000ULL,40000ULL}) {
  const auto phase=c::completion::phase(100000+age,100000);
  const auto state=c::native_completion_countdown(900000,250000,99000,100000,100000+age,phase);
  check(state.valid&&state.clockMs==251000+age,"completion native clock advances after frozen score elapsed");
  check(state.anchorMs==(phase==6?251000:phase==7?256000:state.clockMs),"stable completion phase anchor");
  check(state.durationMs==(phase==6?5000:phase==7?30000:900000),"native5s/30s deadlines");
  check(state.remainingMs==(phase==6?5000:phase==7?30000:0)&&state.running==(phase!=8),"orbit countdown stops");
  sa::Snapshot snapshot{};snapshot.nativeCountdown=true;snapshot.nativeCountdownRunning=state.running;
  check(clk::from_milliseconds(state.durationMs,snapshot.nativeCountdownDurationTicks)
   &&clk::from_milliseconds(state.remainingMs,snapshot.nativeCountdownRemainingTicks)
   &&clk::from_milliseconds(state.anchorMs,snapshot.nativeCountdownAnchorTicks),"completion native ticks");
  std::array<std::byte,64> data{};bits::Writer writer(data);
  check(sa::write_auth_body(writer,snapshot,0,18,2,false)&&writer.bit_count()==386,"completion body width");
  std::size_t length{};check(writer.finish(length),"completion body finish");bits::Reader reader(data);
  read(reader,1);read(reader,64);read(reader,64);read(reader,64);read(reader,64);
  check(read(reader,64)==snapshot.nativeCountdownAnchorTicks,"completion anchor survives wire");
  char path[160]{};std::snprintf(path,sizeof(path),"build/native-pvp-20261002/ui/countdown-completion-%llu.bin",age);
  std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(length));
 }
 for(std::uint64_t late:{1000ULL,15000ULL,300000ULL})for(std::uint64_t age:{0ULL,4999ULL,5000ULL,34999ULL,35000ULL}) {
  const auto delivery=99000+late;const auto phase=c::completion::phase(delivery+age,delivery);
  const auto state=c::native_completion_countdown(900000,250000,99000,delivery,delivery+age,phase);
  check(state.valid&&state.clockMs==250000+late+age,"late first delivery continuous clock");
  const auto anchor=phase==6?250000+late:phase==7?255000+late:state.clockMs;
  check(state.anchorMs==anchor,"retry and polls never reset committed deadline");
  const auto nativeRemaining=state.running&&state.clockMs>=state.anchorMs
    ? state.remainingMs-(state.clockMs-state.anchorMs):state.remainingMs;
  check(nativeRemaining==(age<5000?5000-age:age<35000?35000-age:0),"exact late first publication boundary");
  sa::Snapshot snapshot{};snapshot.nativeCountdown=true;snapshot.nativeCountdownRunning=state.running;
  check(clk::from_milliseconds(state.durationMs,snapshot.nativeCountdownDurationTicks)
   &&clk::from_milliseconds(state.remainingMs,snapshot.nativeCountdownRemainingTicks)
   &&clk::from_milliseconds(state.anchorMs,snapshot.nativeCountdownAnchorTicks),"late native ticks");
  std::array<std::byte,64> data{};bits::Writer writer(data);
  check(sa::write_auth_body(writer,snapshot,0,18,2,false)&&writer.bit_count()==386,"late wire width");
  std::size_t length{};check(writer.finish(length),"late finish");char path[160]{};
  std::snprintf(path,sizeof(path),"build/native-pvp-20261002/ui/countdown-late-%llu-%llu.bin",late,age);
  std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(length));
 }
 const auto aborted=c::native_completion_countdown(900000,250000,99000,100000,100000,6);
 const auto retried=c::native_completion_countdown(900000,250000,99000,115000,115000,6);
 check(aborted.remainingMs==retried.remainingMs&&retried.anchorMs-aborted.anchorMs==15000,
  "discarded first delivery does not spend client's completion interval");
 check(!c::native_completion_countdown(900000,UINT64_MAX,1,2,3,6).valid,"completion clock overflow rejected");
 std::puts("PASS: native countdown exact menu5/15/20minutes, playing/results,386bit schema");
}
