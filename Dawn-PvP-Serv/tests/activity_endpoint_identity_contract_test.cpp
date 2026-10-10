#include <array>
#include <cstdio>
#include "server/gameplay/group/activity_endpoint_identity.h"
namespace binding=dawn::server::gameplay::group::activity_endpoint_identity;
int main() {
    std::array<binding::Candidate,4> rows{{{101,0x7F000001,3000,true},
        {202,0x7F000001,3001,true},{303,0x7F000001,3000,false},{101,0x7F000001,3000,true}}};
    bool ok=binding::resolve(rows,0x7F000001,3000)==101
        && binding::resolve(rows,0x7F000001,3001)==202
        && binding::resolve(rows,0x7F000001,3002)==0
        && binding::resolve(rows,0x7F000002,3000)==0;
    rows[2].verifiedCentral=true;
    ok=ok && binding::resolve(rows,0x7F000001,3000)==0;
    rows[2].platform=101;
    ok=ok && binding::resolve(rows,0x7F000001,3000)==101;
    for(auto& row:rows)row.verifiedCentral=false;
    ok=ok && binding::resolve(rows,0x7F000001,3000)==0;
    std::puts(ok?"PASS: public activity identity requires unique verified central endpoint; no IP-only, unverified, order or conflict fallback"
                :"FAIL: public activity endpoint binding");
    return ok?0:1;
}
