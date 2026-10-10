#include "../dawn/src/server/bap/remote_appearance_policy.h"
#include <cstdio>
#include <cstring>
using namespace dawn::server::bap::remote_appearance;
int main(){
    Tracker t{};const Published a{101,1001,0,true},b{102,1002,0,true};
    const auto check=[](bool ok,const char* text){if(!ok)std::printf("FAIL: %s\n",text);return ok;};
    if(!check(t.full(a,400)&&t.full(b,400),"independent subscribers"))return 1;
    t.changed(101);t.changed(999);
    if(!check(t.find(101)->dirty&&!t.find(102)->dirty&&!Tracker::due(*t.find(101),399)
        &&Tracker::due(*t.find(101),400),"exact account and initial readiness gate"))return 1;
    const auto before=t;
    if(!check(!t.incremental(a,{102,1001,1,true})&&!t.incremental(a,{101,1001,2,true})
        &&!t.incremental(a,{101,0,1,true})&&std::memcmp(&before,&t,sizeof t)==0,
        "wrong root/version/character cannot commit"))return 1;
    if(!check(t.incremental(a,{101,1001,1,true})&&!t.find(101)->dirty
        &&t.find(102)->state.version==0,"one committed incremental, no cross-root ladder"))return 1;
    t.changed(101);t.unsubscribe(101);
    if(!check(!t.find(101)&&t.find(102),"unsubscribe cancels only exact source"))return 1;
    if(!check(t.full(a,500)&&t.find(101)->state.version==0&&!t.find(101)->dirty,
        "resubscribe resets from its own full snapshot"))return 1;
    for(unsigned i=0;i<30;++i)if(!t.full({200+i,2000+i,0,true},0))return 1;
    if(!check(!t.full({999,9999,0,true},0)&&t.find(101)&&t.find(102),"capacity cannot evict active subscribers"))return 1;
    std::puts("PASS: remote appearance exact-root versions, delayed readiness, retry-preservation, unsubscribe, resubscribe, bounded capacity");
}
