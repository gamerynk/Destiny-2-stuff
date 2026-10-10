#pragma once
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <span>
#include "../../../middleware/gameplay/peer/join_messages.h"
namespace dawn::server::gameplay::group::activity_endpoint_identity {
struct Candidate {
    std::uint64_t platform{};
    std::uint32_t address{};
    std::uint16_t port{};
    bool verifiedCentral{};
};
// exact, already verified central endpoint only; never infer from join order or IP alone
inline std::uint64_t resolve(std::span<const Candidate> candidates,std::uint32_t address,
                            std::uint16_t port) noexcept {
    std::uint64_t result{};
    for (const auto& peer:candidates) {
        if (!peer.verifiedCentral || peer.platform==0 || peer.address!=address || peer.port!=port) continue;
        if (result!=0 && result!=peer.platform) return 0;
        result=peer.platform;
    }
    return result;
}

using NativeMembers=middleware::gameplay::peer::JoinMembers;
using NativePlayer=middleware::gameplay::peer::JoinPlayer;
// select by the link's own NetAddr and explicit peer->player index, never order or IP
inline const NativePlayer* select_player(const NativeMembers& members,
    const std::array<std::byte,middleware::gameplay::descriptor::kNetAddrSize>& linkAddress) noexcept {
    if (members.peerCount>members.peers.size() || members.playerCount>members.players.size()) return nullptr;
    const auto method=static_cast<unsigned>(linkAddress.back());
    if (method>7) return nullptr;
    const std::size_t bytes=method>=6?85:41;
    const NativePlayer* selected=nullptr;
    for (std::size_t i=0;i<members.peerCount;++i) {
        const auto& peer=members.peers[i];
        if (peer.address.back()!=linkAddress.back()
            || !std::equal(peer.address.begin(),peer.address.begin()+bytes,linkAddress.begin())) continue;
        if (selected || peer.playerIndex<0
            || static_cast<std::size_t>(peer.playerIndex)>=members.playerCount) return nullptr;
        selected=&members.players[static_cast<std::size_t>(peer.playerIndex)];
    }
    if (!selected || selected->playerId==0) return nullptr;
    return selected;
}
inline std::uint64_t platform(const NativePlayer& player) noexcept {
    std::uint64_t value{};
    std::memcpy(&value,player.identity.data(),sizeof(value));
    return value;
}
struct Participant {
    std::uint64_t platformId{};
    std::uint64_t playerId{};
    std::uint64_t accountId{};
    std::uint64_t memberKey{};
    bool current{};
};
// native join claims aren't credentials; match both account/platform and player incarnation
inline std::size_t resolve_participant(std::span<const Participant> participants,
                                     const NativePlayer& player) noexcept {
    std::size_t found=participants.size();
    const auto nativePlatform=platform(player);
    if (nativePlatform==0 || player.playerId==0) return found;
    for (std::size_t i=0;i<participants.size();++i) {
        const auto& p=participants[i];
        if (!p.current || p.platformId!=nativePlatform || p.playerId!=player.playerId
            || p.accountId==0 || p.memberKey==0) continue;
        if (found!=participants.size()) return participants.size();
        found=i;
    }
    return found;
}
}
