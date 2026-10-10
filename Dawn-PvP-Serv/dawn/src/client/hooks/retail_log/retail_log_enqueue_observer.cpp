#include "retail_log_enqueue_observer.h"
#include "multiplayer_capture_policy.h"
#include "../../../core/logging/log.h"
#include <array>
#include <cstdio>
#include <string_view>

namespace dawn::client::hooks::retail_log {
namespace {
using Enqueue=void(__fastcall*)(std::int32_t,const char*) noexcept;
thread_local bool g_inObserver{};
SRWLOCK g_captureLock{SRWLOCK_INIT};
multiplayer_capture::Budget g_budget{};
std::uintptr_t g_image{};std::size_t g_imageSize{};
// truncation refuses the line so a sensitive suffix is never hidden by the bound
std::size_t sanitize(const char* text,std::array<char,320>& out) noexcept {
 __try {
  for(std::size_t i=0;i<out.size();++i) {
   const char c=text[i];if(c=='\0')return i;
   if(i==out.size()-1)return 0;
   out[i]=(c=='\r'||c=='\n'||c=='\t')?' ':c;
  }
 } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
 return 0;
}
void capture_line(std::int32_t site,const char* text) noexcept {
 std::array<char,320> copy{};const auto size=sanitize(text,copy);
 const std::string_view nativeLine(copy.data(),size);
 const auto reason=multiplayer_capture::classify(nativeLine);
 const auto fingerprint=multiplayer_capture::fingerprint(nativeLine);
 if(reason==multiplayer_capture::Reason::none||!core::log::accepts(core::log::Channel::client,core::log::Level::info))return;
 AcquireSRWLockExclusive(&g_captureLock);
 const bool allowed=g_budget.take(GetTickCount64(),site,fingerprint,reason==multiplayer_capture::Reason::bucket);
 const auto image=g_image;const auto imageSize=g_imageSize;
 ReleaseSRWLockExclusive(&g_captureLock);
 if(!allowed)return;
 std::array<void*,8> frames{};const auto count=CaptureStackBackTrace(0,8,frames.data(),nullptr);
 std::array<char,768> line{};
 const int prefix=std::snprintf(line.data(),line.size(),"ev=native_network_failure reason=%s site=%d thread=%lu fingerprint=%016llX frames=%u rvas=",multiplayer_capture::name(reason),site,GetCurrentThreadId(),static_cast<unsigned long long>(fingerprint),static_cast<unsigned>(count));
 if(prefix<=0||static_cast<std::size_t>(prefix)>=line.size())return;
 auto used=static_cast<std::size_t>(prefix);
 for(USHORT i=0;i<count;++i) {
  const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
  const bool inside=image&&address>=image&&address-image<imageSize;
  const int n=inside?std::snprintf(line.data()+used,line.size()-used,"%s%llX",i?",":"",static_cast<unsigned long long>(address-image))
   :std::snprintf(line.data()+used,line.size()-used,"%sexternal",i?",":"");
  if(n<=0||static_cast<std::size_t>(n)>=line.size()-used)return;
  used+=static_cast<std::size_t>(n);
 }
 const bool safe=multiplayer_capture::safe_text(nativeLine);
 constexpr std::string_view redacted="[redacted native diagnostic]";
 const auto output=safe?nativeLine:redacted;
 const int tail=std::snprintf(line.data()+used,line.size()-used," text=%.*s",static_cast<int>(output.size()),output.data());
 if(tail<=0||static_cast<std::size_t>(tail)>=line.size()-used)return;
 used+=static_cast<std::size_t>(tail);
 core::log::write(core::log::Channel::client,core::log::Level::info,{line.data(),used});
}
__declspec(noinline) void __fastcall enqueue_body(std::int32_t site,const char* text) noexcept {
 const bool outer=!g_inObserver;g_inObserver=true;
 const auto original=reinterpret_cast<Enqueue>(g_handle.original);
 if(original)original(site,text);
 if(outer){if(site!=-1&&text)capture_line(site,text);g_inObserver=false;}
}
}
void begin_multiplayer_capture() noexcept {
 std::uintptr_t image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));std::size_t size{};
 __try {
  const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  if(image&&dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>0&&dos->e_lfanew<0x100000) {
   const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+static_cast<std::uintptr_t>(dos->e_lfanew));
   if(nt->Signature==IMAGE_NT_SIGNATURE&&nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&nt->OptionalHeader.SizeOfImage<=0x40000000)size=nt->OptionalHeader.SizeOfImage;
  }
 } __except(EXCEPTION_EXECUTE_HANDLER){size=0;}
 AcquireSRWLockExclusive(&g_captureLock);g_image=size?image:0;g_imageSize=size;g_budget.begin(GetTickCount64());ReleaseSRWLockExclusive(&g_captureLock);
}
void* enqueue_entry_point() noexcept{return reinterpret_cast<void*>(&enqueue_body);}
void assert_verbosity() noexcept {}
void register_schema_marker(std::uint32_t marker) noexcept{(void)marker;}
}
