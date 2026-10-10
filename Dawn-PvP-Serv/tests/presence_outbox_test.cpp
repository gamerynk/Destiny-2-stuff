#include "steam/runtime/social/presence_outbox.h"
#include <cstdio>
#include <string>
namespace p = dawn::steam::runtime::social::presence_outbox;
int main() {
    p::Outbox q; int checks=0; bool ok=true;
    const auto check=[&](bool value,const char* name){++checks;if(!value){ok=false;std::printf("FAIL %s\n",name);}};
    check(!q.pending(),"empty");
    check(!q.set("","x")&&!q.set(std::string(64,'x'),"x")&&!q.set("k",std::string(256,'x')),"bounds");
    check(!q.set(std::string("x\0y",3),"v")&&!q.set("k",std::string("v\0z",3)),"embedded nul");
    check(q.set("status","Tower"),"set"); auto tower=q.next();
    check(q.set("status","Orbit")&&q.next().revision!=tower.revision,"coalesce");
    q.complete(tower,true); check(q.pending()&&std::string(q.next().value.data())=="Orbit","old completion preserves new state");
    auto orbit=q.next(); q.complete(orbit,false); check(q.next().revision==orbit.revision,"failed write retained");
    q.complete(orbit,true); check(!q.pending(),"ack");
    check(q.set("status","Orbit")&&!q.pending(),"unchanged clean no request");
    check(q.set("status","")&&q.pending(),"deletion"); q.complete(q.next(),true);
    check(!q.pending(),"deletion ack");
    check(q.set("connect","old"),"preclear write"); auto old=q.next();
    q.clear(); auto clear=q.next(); check(clear.clear&&clear.revision,"clear barrier");
    check(q.set("connect","new")&&q.next().clear,"new value cannot overtake clear");
    q.complete(old,true); check(q.next().revision==clear.revision,"inflight completion cannot cancel clear");
    q.complete(clear,false); check(q.next().clear,"clear retry");
    q.complete(clear,true); check(!q.next().clear&&std::string(q.next().value.data())=="new","postclear value");
    q.complete(q.next(),true);
    q.clear(); auto clear2=q.next(); q.clear(); q.complete(clear2,true);
    check(q.next().clear&&q.next().revision!=clear2.revision,"superseded clear ack");
    q.complete(q.next(),true);
    for(int i=0;i<20;++i) check(q.set("key"+std::to_string(i),"v"),"twenty keys");
    check(!q.set("overflow","v"),"twenty first rejected");
    for(int i=0;i<20;++i) check(q.set("key"+std::to_string(i),""),"twenty tombstones");
    for(int i=0;i<20;++i) check(q.set("new"+std::to_string(i),"v"),"replacement with pending tombstones");
    int writes=0; while(q.pending()) {q.complete(q.next(),true);++writes;}
    check(writes==40,"bounded ordered drain including deletions");
    q={}; check(!q.pending(),"shutdown reset");
    std::printf("%s %d presence outbox checks\n",ok?"PASS":"FAIL",checks); return ok?0:1;
}
