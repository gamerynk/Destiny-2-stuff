#include <cassert>
#include <fstream>
#include <vector>
#include <iostream>
#include "../dawn/src/client/hooks/queuez/input_preferences_data.h"
namespace p = dawn::client::hooks::queuez::input_preferences;
int main(int argc, char** argv) {
 assert(argc == 2);
 p::Document d{}; std::ifstream f(argv[1], std::ios::binary);
 assert(f.read(reinterpret_cast<char*>(&d), sizeof d));
 assert(p::valid(d, d.server, d.account) && d.values.mouse == 7);
 assert(!p::valid(d, d.server + 1, d.account));
 assert(!p::valid(d, d.server, d.account + 1));
 auto bad=d; bad.values.mouse=8; assert(!p::valid(bad,d.server,d.account));
 bad=d; bad.version=2;bad.checksum=p::checksum(bad);assert(!p::valid(bad,d.server,d.account));
 auto values=d.values; values.ads=std::numeric_limits<float>::quiet_NaN();assert(!p::valid(values));
 values=d.values;values.mouse=0;assert(!p::valid(values));
 std::vector<std::byte> account(p::kAccountSize, std::byte{0xAB});
 auto initial=account; assert(p::restore(account,d.values));assert(p::capture(account)==d.values);
 for(std::size_t i=0;i<account.size();++i) {
  bool input=i==2970||(i>=2972&&i<2980)||(i>=3648&&i<3652)||i==3660||(i>=3664&&i<3904);
  if(!input) assert(account[i]==initial[i]);
 }
 std::array<std::byte,0x590> owner{}; assert(p::restore_owner(owner,d.values));
 assert(p::capture_fields(owner,0x1c,0x2c8)==d.values);
 const int changed=8; std::memcpy(owner.data()+0x24,&changed,4);
 const std::uint32_t key=0x00740021; std::memcpy(owner.data()+0x2d8+16*4,&key,4);
 auto edited=p::capture_fields(owner,0x1c,0x2c8);assert(edited.mouse==8&&edited.keys[16]==key);
 d.values=edited;d.checksum=p::checksum(d);assert(p::valid(d,d.server,d.account));
 std::fill(account.begin(),account.end(),std::byte{});assert(p::restore(account,d.values));assert(p::capture(account)==edited);
 assert(!p::restore(std::span(account).first(100),d.values));
 std::cout<<"input_preferences_test PASS (live fixture, isolation, corruption, field scope, restart/edit)\n";
}
