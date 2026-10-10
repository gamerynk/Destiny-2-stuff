#include "middleware/signon/config/signon_config_blob.h"
#include "middleware/signon/response.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <vector>
using Bytes = std::span<const std::byte>;
static void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); } }
static std::uint64_t varint(Bytes b, std::size_t& at) {
    std::uint64_t v{};
    for(unsigned shift=0;shift<64;shift+=7) {
        check(at<b.size(),"bounded protobuf varint");
        auto c=std::to_integer<unsigned>(b[at++]);v|=std::uint64_t(c&127)<<shift;
        if(!(c&128))return v;
    }
    check(false,"valid protobuf varint");return 0;
}
static Bytes field(Bytes b,unsigned wanted) {
    std::size_t at{};
    while(at<b.size()) {
        auto key=varint(b,at);
        if((key&7)==0) { (void)varint(b,at);continue; }
        check((key&7)==2,"expected protobuf wire type");
        auto size=varint(b,at);check(size<=b.size()-at,"bounded protobuf bytes");
        auto value=b.subspan(at,static_cast<std::size_t>(size));at+=static_cast<std::size_t>(size);
        if((key>>3)==wanted)return value;
    }
    return {};
}
static std::uint32_t le32(Bytes b,std::size_t at) {
    check(at+4<=b.size(),"bounded native field");
    std::uint32_t v{};for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(b[at+i])<<(i*8);return v;
}
int main(int argc,char** argv) {
    namespace sg=dawn::middleware::signon;
    dawn::state::SignOnState s{};s.bootstrapTokenPresent=true;s.relayAddress=0x7f000001;s.relayPort=30977;
    for(std::size_t i=0;i<s.bootstrapToken.size();++i)s.bootstrapToken[i]=std::byte(i);
    std::array<std::byte,sg::config::kBlobSize> blob{};
    check(sg::config::build(s.bootstrapToken,blob),"native config encoder");
    check(blob.size()==129&&le32(blob,0)==0x123456&&le32(blob,4)==0x7B620F,"native keyed header");
    check(blob[8]==std::byte{16}&&std::equal(s.bootstrapToken.begin(),s.bootstrapToken.end(),blob.begin()+9),"token retained");
    check(le32(blob,25)==0x206&&le32(blob,117)==0x1A39E5&&le32(blob,125)==0,"native keys and terminator");
    const std::array<float,22> reset{0,0,.8F,.8F,.8F,0,0,0,.9F,0,0,0,0,0,0,0,0,.7F,.7F,.7F,.7F,.9F};
    for(std::size_t i=0;i<22;++i) {
        float expected=reset[i];
        if(i==1||i==2||i==16)expected=.95F;
        if(i==7)expected=.73F;
        if(i==12)expected=.7F;
        if(i==13)expected=.75F;
        check(std::bit_cast<float>(le32(blob,29+i*4))==expected,"selected combat priorities and unrelated defaults");
    }
    const auto urgency=std::bit_cast<float>(le32(blob,121));
    check(urgency==.04F,"native authored far initial priority qualifies");
    check(51.F/1023.F>urgency && 204.F/1023.F>urgency && .5F>urgency,
          "far initial, near initial and changed movement exceed native strict threshold");
    check(!(0.F>urgency),"zero priority remains nonurgent");
    std::array<std::byte,1024> output{};std::size_t written{};
    dawn::state::entitlements::Table entitlements{};
    check(sg::encode_success(s,entitlements,60,1,output,written),"whole SignOn response");
    auto common=field(Bytes(output).first(written),4);
    auto emitted=field(common,3);
    check(common.size()==136&&emitted.size()==129&&std::equal(emitted.begin(),emitted.end(),blob.begin()),"nested lengths cross 127 correctly");
    auto tokenOnlySize=written;
    for(std::size_t n=0;n<tokenOnlySize;++n) {
        written=999;check(!sg::encode_success(s,entitlements,60,1,std::span(output).first(n),written)&&written==0,"bounded response failure");
    }
    const std::string maxUrl="https://configured.example/"+std::string(100,'a');
    const sg::ContentConfiguration content{maxUrl,"01234567-89ab-cdef-0123-456789abcdef"};
    std::array<std::byte,sg::config::kBlobCapacity> fullBlob{};std::size_t fullSize{};
    check(sg::config::build(s.bootstrapToken,content.url,content.guid,fullBlob,fullSize),"extended native blob");
    check(sg::encode_success(s,entitlements,60,1,output,written,content),"extended whole SignOn response");
    emitted=field(field(Bytes(output).first(written),4),3);
    check(emitted.size()==fullSize&&std::equal(emitted.begin(),emitted.end(),fullBlob.begin()),"extended nested framing exact");
    const auto fullResponseSize=written;
    for(std::size_t n=0;n<fullResponseSize;++n) {
        written=999;check(!sg::encode_success(s,entitlements,60,1,std::span(output).first(n),written,content)
            &&written==0,"extended bounded response failure");
    }
    s.bootstrapTokenPresent=false;
    check(sg::encode_success(s,entitlements,60,1,output,written),"token-absent response");
    check(field(field(Bytes(output).first(written),4),3).empty(),"never emit config without bootstrap token");
    blob.fill(std::byte{0xA5});auto original=blob;
    check(!sg::config::build(Bytes(s.bootstrapToken).first(15),blob)&&blob==original,"bad token fails before mutation");
    check(!sg::config::build(s.bootstrapToken,std::span(blob).first(128))&&blob==original,"short storage fails before mutation");
    check(sg::config::build(s.bootstrapToken,blob),"restore synthetic fixture");
    if(argc>1) { std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(blob.data()),blob.size());check(bool(f),"write synthetic fixture"); }
    std::puts("PASS: native SignOn keys, token preservation, response framing, absent token and every short response capacity");
}
