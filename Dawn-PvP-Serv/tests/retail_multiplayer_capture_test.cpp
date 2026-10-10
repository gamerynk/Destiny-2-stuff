#include "client/hooks/retail_log/multiplayer_capture_policy.h"
#include <cstdio>
#include <cstdlib>
using namespace dawn::client::hooks::retail_log::multiplayer_capture;
int checks{};void check(bool v,const char* label){++checks;if(!v){std::printf("FAIL %s\n",label);std::exit(1);}}
int main(){
 check(classify("Rabbit") == Reason::bucket,"Rabbit terminal");
 check(classify("KTOBUCKET reason join_failed") == Reason::bucket,"native bucket");
 check(classify("networking:session peer timed out") == Reason::timeout,"context timeout");
 check(classify("group refused connection") == Reason::refusal,"refusal");
 check(classify("peer disconnected") == Reason::disconnect,"disconnect");
 check(classify("session setup failed") == Reason::failure,"session failure");
 check(classify("texture failed to load") == Reason::none,"unrelated failure");
 check(classify("peer joined successfully") == Reason::none,"progress excluded");
 for(auto secret:{"peer failed password=abc","Rabbit auth-token=abc","session failed certificate=abc","peer failed private key abc","Rabbit payload dump","peer failed 0xDEADBEEF","peer failed 0123456789ABCDEF","Rabbit bearer abc","Rabbit secret=abc","Rabbit -----BEGIN KEY"})check(!safe_text(secret),"sensitive text redacted");
 check(!safe_text(std::string_view("Rabbit\x01",7)),"binary text redacted");
 check(classify("Rabbit password=abc")==Reason::bucket&&!safe_text("Rabbit password=abc"),"credential Rabbit retains metadata");
 check(classify("network timeout session=0xDEADBEEF")==Reason::timeout&&!safe_text("network timeout session=0xDEADBEEF"),"hex timeout retains metadata");
 check(safe_text("session failed to join"),"plain failure text retained");
 check(classify("token=abc payload0xDEADBEEF")==Reason::none,"sensitive nonfailure remains excluded");
 std::array<char,320> longLine{};longLine.fill('a');check(classify({longLine.data(),longLine.size()})==Reason::none,"truncation refused");
 Budget b;check(!b.take(1,1,1,false),"disabled budget");b.begin(1000);
 check(!b.take(999,1,1,false),"backward clock");
 for(int i=0;i<8;++i)check(b.take(1000,i,static_cast<unsigned>(i),false),"eight events");
 check(!b.take(1000,9,9,false),"ninth same second");
 check(b.take(2000,0,0,false),"one repeat next second");check(!b.take(3000,0,0,false),"third identical suppressed");
 b.begin(0);for(int i=0;i<112;++i)check(b.take(static_cast<unsigned long long>(i)*1000,i,static_cast<unsigned>(i),false),"normal quota");
 check(!b.take(113000,200,200,false),"normal cannot spend terminal reserve");
 for(int i=0;i<16;++i)check(b.take(114000+static_cast<unsigned long long>(i)*1000,300+i,300+static_cast<unsigned>(i),true),"terminal reserve");
 check(!b.take(140000,500,500,true),"total cap");
 b.begin(0);check(b.take(Budget::duration-1,1,1,true),"before expiry");check(!b.take(Budget::duration,2,2,true),"expiry boundary");
 b.begin(500);check(b.take(500,1,1,false),"reset starts fresh dedupe");
 std::printf("PASS %d checks\n",checks);
}
