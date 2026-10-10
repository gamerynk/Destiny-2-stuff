#include "middleware/web_service/messages/opcode601/opcode601_codec.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/encoding/bit_reader.h"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace w=dawn::middleware::web_service;
namespace c=w::messages::opcode601;
namespace b=dawn::middleware::encoding::bits;
unsigned checks{};
void check(bool ok){++checks;if(!ok){std::printf("FAIL %u\n",checks);std::exit(1);}}
int main(){
 std::array<std::byte,17> body{}; b::Writer writer(body);std::size_t bytes{};
 check(writer.write(5,5)&&writer.write(0xFEDCBA98,32)&&writer.write(0x8877665544332211ULL,64)&&writer.write(0x80000007,32)&&writer.finish(bytes)&&bytes==17);
 w::Message message{601,0x12345678,body};c::Request request{};
 check(c::parse_request(message,request)&&request.rawKind==5&&request.rawSourceTag==0xFEDCBA98&&request.sourceHandle==0x8877665544332211ULL&&request.rawSequence==0x80000007);
 std::array<std::byte,27> response{};std::size_t written{};
 check(c::encode_response(message,response,written)&&written==27);
 check(response[0]==std::byte{2}&&response[1]==std::byte{0x59}&&response[2]==std::byte{0x12}&&response[5]==std::byte{0x78});
 b::Reader reader(std::span(response).subspan(6));std::uint64_t value{};
 check(reader.read(5,value)&&value==1);check(reader.read(32,value)&&value==0);
 check(reader.read(32,value)&&value==request.rawSourceTag);check(reader.read(64,value)&&value==request.sourceHandle);check(reader.read(32,value)&&value==request.rawSequence);check(reader.read(2,value)&&value==0);
 for(std::size_t size=0;size<17;++size){message.payload=std::span(body).first(size);request.sourceHandle=99;written=99;check(!c::parse_request(message,request)&&request.sourceHandle==0);check(!c::encode_response(message,response,written)&&written==0);}
 std::array<std::byte,18> extended{};message.payload=extended;check(!c::parse_request(message,request));
 message.payload=body;body[16]|=std::byte{1};check(!c::parse_request(message,request));body[16]&=std::byte{0xF8};
 message.opcode=602;check(!c::parse_request(message,request));message.opcode=601;
 check(!c::encode_response(message,std::span(response).first(26),written)&&written==0);
 std::array<std::byte,27> second{};check(c::encode_response(message,second,written));check(c::encode_response(message,response,written)&&response==second);
 std::printf("opcode601: %u checks passed\n",checks);
}
