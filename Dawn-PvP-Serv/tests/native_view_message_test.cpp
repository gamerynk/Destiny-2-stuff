#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "middleware/gameplay/group/view_message.h"
namespace bits=dawn::middleware::encoding::bits;
namespace wire=dawn::middleware::gameplay::group;
unsigned checks{};
void require(bool x){++checks;if(!x){std::fprintf(stderr,"check%u failed\n",checks);std::exit(1);}}
void oracle(unsigned kind,std::int32_t rev,bool list,const char* golden,std::size_t n){
 std::array<std::byte,64> out{};bits::Writer w(out);wire::ViewEstablishment v{};
 v.kind=static_cast<std::uint8_t>(kind);v.hasOptionalValue=true;v.optionalValue=rev;v.hasList=list;v.listCount=list?15:0;
 for(unsigned i=0;i<15;++i)v.list[i]=static_cast<std::byte>(i);
 v.sessionToken=kind==5?0x9eaa00010020002bULL:0x9eaa00010020002aULL;
 require(wire::write_view(w,v));require(w.bit_count()==n);
 for(std::size_t i=0;i<(n+7)/8;++i){unsigned x{};require(sscanf_s(golden+2*i,"%2x",&x)==1);require(out[i]==static_cast<std::byte>(x));}
 bits::Reader r(out,n);wire::ViewEstablishment decoded{};require(wire::read_view(r,decoded));require(decoded.kind==kind&&decoded.sessionToken==v.sessionToken);require(decoded.hasOptionalValue==(rev!=-1));
}
int main(){
 // golden bytes from copied native 16DEE90 execution, not this encoder
 oracle(1,-1,false,"24f550000801000150",69);
 oracle(2,0,true,"5000000008f000102030405060708090a0b0c0d0e9eaa00010020002a0",228);
 oracle(4,0,false,"9000000004f550000801000150",101);
 oracle(5,0x7fffffff,false,"b7fffffff4f550000801000158",101);
 for(auto bad:{-2,INT32_MIN}){
  std::array<std::byte,32> out{};bits::Writer w(out);require(w.write(1,3));require(w.write(1,1));require(w.write(static_cast<std::uint32_t>(bad),32));require(w.write(0,1));require(w.write(1,64));
  bits::Reader r(out,101);wire::ViewEstablishment v{};v.sessionToken=77;require(!wire::read_view(r,v));require(v.sessionToken==77);
  bits::Writer encode(out);v.hasOptionalValue=true;v.optionalValue=bad;require(!wire::write_view(encode,v));require(encode.bit_count()==0);
 }
 std::printf("native ID40 codec %u checks passed\n",checks);
}
