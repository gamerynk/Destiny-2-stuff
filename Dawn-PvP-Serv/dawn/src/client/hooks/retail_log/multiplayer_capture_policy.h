#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dawn::client::hooks::retail_log::multiplayer_capture {
enum class Reason { none, bucket, timeout, refusal, disconnect, failure };
inline constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a'-'A')) : c; }
inline bool contains(std::string_view text, std::string_view needle) noexcept {
 if(needle.size()>text.size())return false;
 for(std::size_t i=0;i+needle.size()<=text.size();++i) {
  bool same=true;for(std::size_t j=0;j<needle.size();++j)if(lower(text[i+j])!=needle[j]){same=false;break;}
  if(same)return true;
 }return false;
}
// sensitive lines keep only classified failure metadata; never partially redact raw text
inline bool safe_text(std::string_view text) noexcept {
 if(text.empty()||text.size()>=320)return false;
 constexpr std::array sensitive{"credential","token","authorization","bearer","password","secret","privatekey","private key","private_key","api key","key material","key=","key:","key =","dtls","ticket","auth=","certificate","certpayload","cert_payload","payload","dump","0x","-----begin"};
 for(auto marker:sensitive)if(contains(text,marker))return false;
 std::size_t hexRun{};
 for(char c:text) {
  if(c<' '||static_cast<unsigned char>(c)>=127)return false;
  const char d=lower(c);hexRun=(d>='0'&&d<='9')||(d>='a'&&d<='f')?hexRun+1:0;
  if(hexRun>=16)return false;
 }
 return true;
}
inline Reason classify(std::string_view text) noexcept {
 if(text.empty()||text.size()>=320)return Reason::none;
 if(contains(text,"rabbit")||contains(text,"ktobucket"))return Reason::bucket;
 if(!contains(text,"network")&&!contains(text,"session")&&!contains(text,"peer")&&!contains(text,"group"))return Reason::none;
 if(contains(text,"timeout")||contains(text,"timed out"))return Reason::timeout;
 if(contains(text,"refus")||contains(text,"reject"))return Reason::refusal;
 if(contains(text,"disconnect"))return Reason::disconnect;
 if(contains(text,"fail"))return Reason::failure;
 return Reason::none;
}
inline const char* name(Reason r) noexcept {
 switch(r){case Reason::bucket:return "bucket";case Reason::timeout:return "timeout";case Reason::refusal:return "refusal";case Reason::disconnect:return "disconnect";case Reason::failure:return "failure";default:return "none";}
}
struct Budget {
 static constexpr std::uint64_t duration=20*60*1000;
 struct Seen {std::int32_t site{};std::uint64_t fingerprint{};std::uint8_t repeats{};};
 std::array<Seen,128> seen{};std::size_t seenCount{};
 std::uint64_t start{},second{};std::uint32_t total{},normal{},terminal{},inSecond{};bool started{};
 void begin(std::uint64_t now) noexcept {start=now;second=now/1000;total=normal=terminal=inSecond=0;seenCount=0;started=true;}
 bool take(std::uint64_t now,std::int32_t site,std::uint64_t fingerprint,bool bucket) noexcept {
  if(!started||now<start||now-start>=duration||total>=128)return false;
  if(bucket?terminal>=16:normal>=112)return false;
  if(second!=now/1000){second=now/1000;inSecond=0;}
  if(inSecond>=8)return false;
  std::size_t index=0;
  while(index<seenCount&&(seen[index].site!=site||seen[index].fingerprint!=fingerprint))++index;
  if(index<seenCount&&seen[index].repeats>=2)return false;
  if(index==seenCount){if(seenCount==seen.size())return false;seen[seenCount++]={site,fingerprint,0};}
  ++seen[index].repeats;++inSecond;++total;if(bucket)++terminal;else ++normal;return true;
 }
};
inline std::uint64_t fingerprint(std::string_view text) noexcept {
 std::uint64_t hash=14695981039346656037ULL;for(char c:text){hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;}return hash;
}
}
