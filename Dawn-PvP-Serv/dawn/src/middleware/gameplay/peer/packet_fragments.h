#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::gameplay::peer::fragments {
// PC 86657 16D4B00. Prefix: marker 0, fragmented 1, sequence 6, destination guard 2,
// (count-1) 3, index 3. Body is a slice of the whole established packet, header included
inline constexpr std::size_t kParts=8, kWindows=8, kMaximumPartBytes=1238;
inline constexpr std::size_t kMaximumPacketBytes=kParts*kMaximumPartBytes;
inline constexpr std::uint64_t kLifetimeMs=2000;
struct Prefix final {std::uint8_t sequence{},guard{},count{},index{};};
[[nodiscard]] inline bool read_prefix(std::span<const std::byte> bytes,Prefix& output) noexcept {
    if(bytes.size()<=2)return false;
    const auto first=std::to_integer<unsigned>(bytes[0]);
    const auto second=std::to_integer<unsigned>(bytes[1]);
    if((first&0xC0U)!=0x40U)return false;
    Prefix staged{static_cast<std::uint8_t>(first&63U),static_cast<std::uint8_t>(second>>6),
        static_cast<std::uint8_t>(((second>>3)&7U)+1),static_cast<std::uint8_t>(second&7U)};
    if(staged.index>=staged.count)return false;
    output=staged;return true;
}
enum class Result {refused,pending,duplicate,conflict,capacity,complete};
struct Receipt final {
    Result result{Result::refused};
    std::span<const std::byte> packet{};
};
class Assembly final {
public:
    // 16D4B97 uses min(4062E0(transportMode),1240)-2; some modes use 1226, so never infer 1238
    // as a per-link default
    [[nodiscard]] bool reset(std::uint64_t incarnation,std::uint8_t guard,
                             std::uint16_t partBytes) noexcept {
        for(auto& slot:slots_)slot.clear();
        incarnation_=0;partBytes_=0;guard_=0;lastTick_=0;
        if(!incarnation||guard>3||!partBytes||partBytes>kMaximumPartBytes)return false;
        incarnation_=incarnation;guard_=guard;partBytes_=partBytes;return true;
    }
    [[nodiscard]] Receipt receive(std::uint64_t incarnation,std::span<const std::byte> bytes,
                                  std::uint64_t now) noexcept {
        Prefix prefix{};
        if(!incarnation_||incarnation!=incarnation_||now<lastTick_
            ||!read_prefix(bytes,prefix)||prefix.guard!=guard_)return {};
        const auto body=bytes.subspan(2);
        if(body.size()>partBytes_||(prefix.index+1<prefix.count&&body.size()!=partBytes_))return {};
        lastTick_=now;
        Slot* match=nullptr;Slot* free=nullptr;
        for(auto& slot:slots_) {
            if(slot.occupied&&now-slot.started>kLifetimeMs)slot.clear();
            if(slot.occupied&&slot.sequence==prefix.sequence)match=&slot;
            if(!slot.occupied&&!free)free=&slot;
        }
        if(!match){
            if(!free)return {Result::capacity,{}};
            match=free;match->occupied=true;match->sequence=prefix.sequence;
            match->count=prefix.count;match->started=now;
        }
        if(match->poisoned)return {Result::conflict,{}};
        // refuse a conflicting retransmission for the whole bounded lifetime; never combine a
        // replacement sequence with retained old pieces
        if(match->count!=prefix.count){match->poisoned=true;return {Result::conflict,{}};}
        const auto bit=static_cast<std::uint8_t>(1U<<prefix.index);
        const auto offset=static_cast<std::size_t>(prefix.index)*partBytes_;
        if(match->received&bit){
            if(match->sizes[prefix.index]!=body.size()
                ||!std::equal(body.begin(),body.end(),match->bytes.begin()+offset)){
                match->poisoned=true;return {Result::conflict,{}};
            }
            return {Result::duplicate,match->complete?match->packet():std::span<const std::byte>{}};
        }
        std::copy(body.begin(),body.end(),match->bytes.begin()+offset);
        match->sizes[prefix.index]=static_cast<std::uint16_t>(body.size());
        match->received|=bit;
        if(match->received!=static_cast<std::uint8_t>((1U<<match->count)-1))return {Result::pending,{}};
        match->size=(static_cast<std::size_t>(match->count)-1)*partBytes_+match->sizes[match->count-1];
        // native 16D54B8 consumes the original two leading bits, then 16D56C0 reads the connection
        // guard; accept only the unfragmented established encoding with that same guard
        const auto first=std::to_integer<unsigned>(match->bytes[0]);
        if((first&0xC0U)||((first>>4)&3U)!=guard_){
            match->poisoned=true;return {Result::conflict,{}};
        }
        match->complete=true;return {Result::complete,match->packet()};
    }
private:
    struct Slot final {
        std::array<std::byte,kMaximumPacketBytes> bytes{};
        std::array<std::uint16_t,kParts> sizes{};
        std::uint64_t started{};std::size_t size{};
        std::uint8_t sequence{},count{},received{};
        bool occupied{},poisoned{},complete{};
        void clear() noexcept {sizes={};started=0;size=0;sequence=0;count=0;received=0;
            occupied=false;poisoned=false;complete=false;}
        std::span<const std::byte> packet() const noexcept {return {bytes.data(),size};}
    };
    std::array<Slot,kWindows> slots_{};
    std::uint64_t incarnation_{},lastTick_{};
    std::uint16_t partBytes_{};std::uint8_t guard_{};
};
}
