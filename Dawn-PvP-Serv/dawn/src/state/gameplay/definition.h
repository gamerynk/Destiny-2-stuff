#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::gameplay {

inline constexpr std::size_t kChannelKeySize = 16;
inline constexpr std::size_t kNetAddrBlobSize = 86;
inline constexpr std::size_t kNonceSeedSize = 12;
inline constexpr std::size_t kAssociationCapacity = 4;
inline constexpr std::uint64_t kAssociationTimeoutMs = 10'000;

enum class AssociationStage : std::uint8_t {
    absent,
    pending,
    established,
    failed,
};

struct Endpoint {
    std::uint32_t address{};
    std::uint16_t port{};
};

// outbound nonce base is generated here, inbound base comes in the offer; never alias
// them
struct ProtectedContext {
    std::array<std::byte, kChannelKeySize> key{};
    std::array<std::byte, kNonceSeedSize> outboundBase{};
    std::array<std::byte, kNonceSeedSize> inboundBase{};
    // advanced before every protected send so no two sends share a nonce
    std::uint32_t outboundWordA{};
    std::uint32_t outboundWordB{};
    bool installed{};
};

struct Association {
    Endpoint endpoint{};
    ProtectedContext protection{};
    AssociationStage stage{AssociationStage::absent};
    // word A of the newest accepted offer; an older or equal offer is refused
    std::uint32_t offerWordA{};
    std::uint32_t offerWordB{};
    bool answered{};
    std::uint32_t acceptedWordA{};
    bool acceptedAny{};
    std::uint64_t createdTick{};
    std::uint64_t lastTick{};
};

inline constexpr std::size_t kAckHistory = 127;
inline constexpr std::uint16_t kPacketSequenceModulus = 1024;
// a packet more than half the sequence space behind the head is old, not new
inline constexpr std::uint16_t kPacketSequenceHalf = kPacketSequenceModulus / 2;

inline constexpr std::size_t kViewListCapacity = 16;

// entity output is refused until bound: a mismatched signature makes the peer discard
// every contribution
struct ViewSignature {
    std::array<std::byte, kViewListCapacity> list{};
    std::uint64_t token{};
    std::uint8_t kind{};
    std::uint8_t listCount{};
    bool hasList{};
    bool bound{};
};

// these numbers are the engine's own; do not renumber
enum class PeerStage : std::uint8_t {
    absent = 0,
    allocated = 1,
    teardown = 2,
    connecting = 3,
    establishing = 4,
    connected = 5,
};

inline constexpr std::uint16_t kMessageSequenceModulus = 8192;
inline constexpr std::size_t kReliableSlots = 32;
inline constexpr std::size_t kReliableFragmentBytes = 32;
// deliberate transport ceiling; typed large-message expansion is a separate change
inline constexpr std::size_t kReassemblyCapacity = 1024;

struct ReliableFragment {
    std::array<std::byte, kReliableFragmentBytes> bytes{};
    std::uint16_t sequence{};
    std::uint16_t bitCount{};
    // terminating record form, which may hold a full 256 bits
    bool shortFragment{};
    bool occupied{};
};

struct ReliableQueue {
    std::array<ReliableFragment, kReliableSlots> fragments{};
    std::uint16_t nextSequence{};
    bool started{};
};

// a join overruns this, so a sender that must not be dropped has to retry
inline constexpr std::size_t kOutboundSlots = 128;

struct OutboundFragment {
    std::array<std::byte, kReliableFragmentBytes> bytes{};
    std::uint16_t sequence{};
    std::uint16_t bitCount{};
    bool shortFragment{};
    bool occupied{};
};

inline constexpr std::uint16_t kFirstMessageSequence = 1;

// two public regions overlap during a transition; the rest is headroom
inline constexpr std::size_t kSessionsPerLink = 4;

struct OutboundQueue {
    std::array<OutboundFragment, kOutboundSlots> fragments{};
    // the peer refuses a first record whose sequence is not 1
    std::uint16_t nextSequence{kFirstMessageSequence};
    std::size_t count{};
    // packet sequence the contents were first written into; a resend keeps it
    std::uint16_t sentInPacket{};
    std::size_t sentCount{};
    bool awaitingAcknowledgement{};
    std::array<std::uint16_t, 32> carryingPackets{};
    std::size_t carryingCount{};
};

struct PeerLink {
    Endpoint endpoint{};
    PeerStage stage{PeerStage::absent};
    std::uint32_t localConnectionSequence{};
    std::uint32_t localTransportSequence{};
    std::uint32_t remoteConnectionSequence{};
    std::uint32_t remoteTransportSequence{};
    // the client holds one channel per host peer and multiplexes every region's session on it
    std::array<std::uint64_t, kSessionsPerLink> sessions{};
    std::array<std::byte, kNetAddrBlobSize> remoteAddress{};
    bool remoteAddressPresent{};
    std::uint16_t receiveHead{};
    bool ringInitialized{};
    std::array<bool, kAckHistory> received{};
    std::uint8_t receivedCount{};
    std::uint16_t outboundHead{};
    bool outboundHeadPresent{};
    ReliableQueue large{};
    ReliableQueue small{};
    OutboundQueue outbound{};
    // set once a packet arrives on an already-connected link; messages queued in the same
    // exchange as the peer's establishment are acked before the peer dispatches them
    bool applicationReady{};
    ViewSignature view{};
    std::uint32_t postQueueProbePackets{};
    std::uint32_t postQueueSecondBitPackets{};
    bool acknowledgementOwed{};
    std::uint64_t lastTick{};
    std::uint64_t lastSend{};
};

struct GameplayState {
    std::array<Association, kAssociationCapacity> associations{};
    std::array<PeerLink, kAssociationCapacity> peers{};
    bool endpointBound{};
    Endpoint bound{};
};

} // namespace dawn::state::gameplay
