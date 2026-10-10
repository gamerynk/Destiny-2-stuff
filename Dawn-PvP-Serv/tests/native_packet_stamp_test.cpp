#include "../dawn/src/middleware/gameplay/peer/native_packet_stamp.h"
#include "fixtures/native_stamp_vectors.h"
#include <cassert>
#include <cstdio>
using namespace dawn::middleware::gameplay::peer;
int main(){
    NativeStampResult result{77,77,77};
    assert(!native_packet_stamp({0,false},0,true,0,result)&&result.stamp==77);
    assert(!native_packet_stamp({0,true},1024,true,0,result));
    assert(!native_packet_stamp({0,true},0,true,128,result));
    for(const auto& v:nativeStampVectors){
        assert(native_packet_stamp({v.prior,true},v.low,v.present,v.cursor,result));
        assert(result.stamp==v.stamp&&result.nextHead==v.nextHead);
    }
    assert(native_packet_stamp({0,true},128,true,0,result)&&result.stamp==128);
    assert(native_packet_stamp({0,true},129,true,0,result)&&result.stamp==-895&&result.nextHead==0);
    assert(native_packet_stamp({896,true},0,true,0,result)&&result.stamp==1024);
    assert(native_packet_stamp({895,true},0,true,0,result)&&result.stamp==0&&result.nextHead==895);
    assert(native_packet_stamp({1023,true},0,true,1,result)&&result.stamp==1024&&result.nextHead==1023);
    assert(native_packet_stamp({1023,true},0,false,0,result)&&result.stamp==-1&&result.unwrapped==1024);
    assert(native_packet_stamp({2147483647,true},0,true,0,result)&&result.stamp==(-2147483647-1));
    for(std::int32_t prior=0;prior<1024;++prior)for(std::uint16_t low=0;low<1024;++low){
        auto expected=static_cast<std::int32_t>(low)-prior;
        if(expected>128)expected-=1024;else if(expected<=-896)expected+=1024;
        assert(native_packet_stamp({prior,true},low,true,0,result));
        assert(result.stamp==prior+expected);
    }
    std::puts("native packet stamp: 1048576 unwrap cases, asymmetric bounds, cursor and DWORD wrap passed");
}
