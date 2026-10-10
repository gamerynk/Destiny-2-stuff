#include "peer_transport.h"

#include <Windows.h>
#include "../transport_capacity.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>

#include "../../../core/filesystem/path.h"
#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/encoding/bit_raw.h"
#include "../../../middleware/encoding/bit_reader.h"
#include "../../../middleware/encoding/bit_writer.h"
#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/gameplay/group/member_messages.h"
#include "../../../middleware/gameplay/group/parameter_messages.h"
#include "../../../middleware/gameplay/peer/connect_messages.h"
#include "../../../middleware/gameplay/peer/established_packet.h"
#include "../../../middleware/gameplay/peer/join_messages.h"
#include "../../../middleware/gameplay/peer/peer_container.h"
#include "../../../middleware/gameplay/peer/reliable_assembly.h"
#include "../../../middleware/gameplay/peer/reliable_carriers.h"
#include "../../../middleware/gameplay/peer/reliable_send_batch.h"
#include "../../../middleware/gameplay/peer/receive_history.h"
#include "../../../middleware/gameplay/peer/inbound_ack_context.h"
#include "../../../middleware/gameplay/peer/native_packet_stamp.h"
#include "../../../middleware/gameplay/peer/packet_fragments.h"
#include "../association/association_host.h"
#include "../dtls/dtls_host.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_log.h"
#include "../group/group_host.h"

namespace dawn::server::gameplay::peer {

namespace {

namespace wire = middleware::gameplay::peer;
namespace group_wire = middleware::gameplay::group;
namespace bits = middleware::encoding::bits;

// records go first; the engine association answers only when no record association exists
[[nodiscard]] bool send_transport(const state::gameplay::Endpoint& to,
                                  std::span<const std::byte> payload) noexcept {
    return dtls::send_payload(to, payload) || association::send_payload(to, payload);
}

constexpr std::size_t kReplyCapacity = 1024;
constexpr unsigned kMarkerShift = 7;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFF;
constexpr std::uint32_t kSequenceGuardBase = 4;
constexpr std::uint16_t kDelaySentinel = 1023;
constexpr std::uint16_t kPacketSequenceModulus = state::gameplay::kPacketSequenceModulus;
constexpr std::uint16_t kFirstPacketSequence = 1;
// smallest head-minus-cursor the peer accepts; this host keeps at most one packet in flight
constexpr std::uint8_t kMinimumHeadCursor = 1;
constexpr std::size_t kMessageReportCapacity = 8;
// the peer discards a packet more than 128 sequences ahead of its window, so don't send faster than it does
constexpr std::uint64_t kResendInterval = 250;
constexpr std::uint8_t kPeerPropertiesId = 31;
constexpr std::uint8_t kPlayerAddId = 34;
constexpr std::uint8_t kPlayerRemoveId = 36;
constexpr std::uint8_t kPlayerPropertiesId = 37;
constexpr std::uint32_t kPlayerPropertiesDeclaredSize = 288;
// ID37 has no player/member/machine/join key; native binds it to the sending channel's
// player, so relaying over another channel aliases roster rows. Quarantine raw relay
constexpr bool kOpaquePlayerPropertiesRelayEnabled = false;
constexpr std::size_t kSessionPublicationDumpCapacity = 128;
constexpr std::size_t kParameterRequestCaptureCapacity = 24;
constexpr std::size_t kPlayerPropertiesCacheCapacity = 64;
constexpr std::size_t kPlayerPropertiesBodyCapacity = state::gameplay::kReassemblyCapacity;
constexpr std::size_t kPlayerPropertiesReplayPerSlice = 4;
constexpr std::uint8_t kExternalProbeWordBits = 64;
SRWLOCK g_parameterRequestCaptureLock{SRWLOCK_INIT};
std::array<state::gameplay::Endpoint, kParameterRequestCaptureCapacity>
    g_parameterRequestCaptureEndpoints{};
std::array<std::uint64_t, kParameterRequestCaptureCapacity> g_parameterRequestCaptureMasks{};
std::array<bool, kParameterRequestCaptureCapacity> g_parameterRequestCaptureOccupied{};

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<state::gameplay::PeerLink, kTransportPeerCapacity> g_peers;
std::array<wire::InboundAckContext, kTransportPeerCapacity> g_ackContexts;
std::array<wire::NativeStampContext, kTransportPeerCapacity> g_nativeStamps;
std::array<std::uint64_t, kTransportPeerCapacity> g_issuedOrdinals;
std::array<wire::fragments::Assembly, kTransportPeerCapacity> g_fragments;
// needs an explicit matched native MTU qualification; without it fragment admission stays disabled
[[nodiscard]] std::uint16_t fragment_part_bytes() noexcept {
    static const std::uint16_t qualified = []() noexcept -> std::uint16_t {
        std::array<char, 16> value{};
        const auto count = GetEnvironmentVariableA("RESTORATION_NATIVE_FRAGMENT_PART_BYTES",
                                                   value.data(), static_cast<DWORD>(value.size()));
        if (count != 4) return 0;
        if (value[0]=='1' && value[1]=='2' && value[2]=='3' && value[3]=='8') return 1238;
        if (value[0]=='1' && value[1]=='2' && value[2]=='2' && value[3]=='6') return 1226;
        return 0;
    }();
    return qualified;
}
[[nodiscard]] std::uint64_t connection_incarnation(const state::gameplay::PeerLink& peer) noexcept {
    return (static_cast<std::uint64_t>(peer.localConnectionSequence) << 32)
           | peer.remoteConnectionSequence;
}
std::uint32_t g_channelId{0};

struct CachedPlayerProperties final {
    std::array<std::byte, kPlayerPropertiesBodyCapacity> body{};
    std::array<state::gameplay::Endpoint, kTransportPeerCapacity> delivered{};
    state::gameplay::Endpoint source{};
    std::uint64_t sessionId{};
    std::uint64_t sequence{};
    std::uint32_t declaredSize{};
    std::size_t bodyBits{};
    std::size_t bodyBytes{};
    std::size_t deliveredCount{};
    bool occupied{};
};

struct PlayerPropertiesReplayTarget final {
    state::gameplay::Endpoint endpoint{};
    std::array<std::uint64_t, state::gameplay::kSessionsPerLink> sessions{};
    std::size_t sessionCount{};
    bool occupied{};
};

std::array<CachedPlayerProperties, kPlayerPropertiesCacheCapacity> g_playerPropertiesCache{};
std::uint64_t g_playerPropertiesSequence{};

// the inner header is byte aligned (6-bit id, 18-bit size) so the body copies from byte three
// observation only; group dispatch still gets the original reader
void report_session_publication_message(const wire::AssembledMessage& message) noexcept {
    if ((message.id != kPeerPropertiesId && message.id != kPlayerAddId
         && message.id != kPlayerRemoveId && message.id != kPlayerPropertiesId)
        || message.bitCount < message.bodyBitOffset
        || message.bodyBitOffset % kByteBits != 0) {
        return;
    }
    const std::size_t bodyBits = message.bitCount - message.bodyBitOffset;
    const std::size_t bodyBytes = (bodyBits + kByteBits - 1U) / kByteBits;
    const std::size_t dumpBytes = (std::min)(bodyBytes, kSessionPublicationDumpCapacity);
    const std::size_t bodyByteOffset = message.bodyBitOffset / kByteBits;
    std::array<char, kSessionPublicationDumpCapacity * 2U + 1U> hex{};
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < dumpBytes; ++index) {
        const unsigned value = std::to_integer<unsigned>(message.bytes[bodyByteOffset + index]);
        hex[index * 2U] = kDigits[(value >> 4U) & 0xFU];
        hex[index * 2U + 1U] = kDigits[value & 0xFU];
    }
    report(core::log::Level::info,
           "ev=gameplay stage=session_publication result=wire id=%u declared=%u total_bits=%zu "
           "body_bits=%zu body_bytes=%zu dumped=%zu hex=%s",
           static_cast<unsigned>(message.id),
           static_cast<unsigned>(message.declaredSize),
           message.bitCount,
           bodyBits,
           bodyBytes,
           dumpBytes,
           hex.data());
}

[[nodiscard]] std::size_t
reserve_parameter_request_capture(const state::gameplay::Endpoint& from,
                                  std::uint64_t requestedMask) noexcept {
    std::size_t result = 0;
    AcquireSRWLockExclusive(&g_parameterRequestCaptureLock);
    for (std::size_t index = 0; index < g_parameterRequestCaptureEndpoints.size(); ++index) {
        if (g_parameterRequestCaptureOccupied[index]
            && g_parameterRequestCaptureEndpoints[index].address == from.address
            && g_parameterRequestCaptureEndpoints[index].port == from.port
            && g_parameterRequestCaptureMasks[index] == requestedMask) {
            ReleaseSRWLockExclusive(&g_parameterRequestCaptureLock);
            return 0;
        }
    }
    for (std::size_t index = 0; index < g_parameterRequestCaptureEndpoints.size(); ++index) {
        if (!g_parameterRequestCaptureOccupied[index]) {
            g_parameterRequestCaptureEndpoints[index] = from;
            g_parameterRequestCaptureMasks[index] = requestedMask;
            g_parameterRequestCaptureOccupied[index] = true;
            result = index + 1U;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_parameterRequestCaptureLock);
    return result;
}

void capture_parameter_request_message(const state::gameplay::Endpoint& from,
                                       const wire::AssembledMessage& message) noexcept {
    if (message.id != group_wire::kParameterRequestId
        || message.bitCount < message.bodyBitOffset
        || message.bodyBitOffset % kByteBits != 0) {
        return;
    }
    const std::size_t messageBytes = (message.bitCount + kByteBits - 1U) / kByteBits;
    const std::size_t bodyBits = message.bitCount - message.bodyBitOffset;
    const std::size_t bodyBytes = (bodyBits + kByteBits - 1U) / kByteBits;
    const std::size_t bodyByteOffset = message.bodyBitOffset / kByteBits;
    if (messageBytes > message.bytes.size()
        || bodyByteOffset + bodyBytes > message.bytes.size()) {
        report(core::log::Level::warn,
               "ev=gameplay stage=parameter_request_capture result=invalid_boundary "
               "endpoint=0x%08X:%u total_bits=%zu body_offset=%zu body_bits=%zu",
               from.address,
               static_cast<unsigned>(from.port),
               message.bitCount,
               message.bodyBitOffset,
               bodyBits);
        return;
    }
    bits::Reader headerReader(
        std::span<const std::byte>(message.bytes.data() + bodyByteOffset, bodyBytes), bodyBits);
    group_wire::ParameterRequestHeader header{};
    if (!group_wire::read_parameter_request(headerReader, header)) {
        return;
    }
    const std::size_t capture =
        reserve_parameter_request_capture(from, header.requestedMask);
    if (capture == 0) {
        return;
    }

    std::array<char, state::gameplay::kReassemblyCapacity * 2U + 1U> hex{};
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < bodyBytes; ++index) {
        const unsigned value =
            std::to_integer<unsigned>(message.bytes[bodyByteOffset + index]);
        hex[index * 2U] = kDigits[(value >> 4U) & 0xFU];
        hex[index * 2U + 1U] = kDigits[value & 0xFU];
    }

    bool complete = false;
    DWORD written = 0;
    core::path::Buffer path{};
    const HMODULE executable = GetModuleHandleW(nullptr);
    if (executable != nullptr && core::path::artifact_directory(executable, path)
        && core::path::append(path, L"\\analysis")
        && (CreateDirectoryW(path.chars.data(), nullptr) != FALSE
            || GetLastError() == ERROR_ALREADY_EXISTS)) {
        std::array<wchar_t, 240> filename{};
        const int length = std::swprintf(
            filename.data(),
            filename.size(),
            L"\\parameter_request.%02zu.tick_%010llu.endpoint_%08X_%u.mask_%08llX.total_%04zu.body_%04zu.bin",
            capture,
            static_cast<unsigned long long>(GetTickCount64()),
            from.address,
            static_cast<unsigned>(from.port),
            static_cast<unsigned long long>(header.requestedMask),
            message.bitCount,
            bodyBits);
        if (length > 0 && core::path::append(path, filename.data())) {
            const HANDLE file = CreateFileW(path.chars.data(),
                                            GENERIC_WRITE,
                                            FILE_SHARE_READ,
                                            nullptr,
                                            CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL,
                                            nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                complete = WriteFile(file,
                                     message.bytes.data(),
                                     static_cast<DWORD>(messageBytes),
                                     &written,
                                     nullptr)
                               != FALSE
                           && written == messageBytes && FlushFileBuffers(file) != FALSE;
                (void)CloseHandle(file);
            }
        }
    }
    report(complete ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=parameter_request_capture result=%s n=%zu "
           "endpoint=0x%08X:%u mask=0x%08llX declared=%u total_bits=%zu body_offset=%zu "
           "body_bits=%zu body_bytes=%zu written=%lu body_hex=%s",
           complete ? "ok" : "write",
           capture,
           from.address,
           static_cast<unsigned>(from.port),
           static_cast<unsigned long long>(header.requestedMask),
           static_cast<unsigned>(message.declaredSize),
           message.bitCount,
           message.bodyBitOffset,
           bodyBits,
           bodyBytes,
           static_cast<unsigned long>(written),
           hex.data());
}

struct PlayerPropertiesRelayResult {
    std::uint64_t sessionId{};
    bool sourceAuthorized{};
    bool suppressed{};
    bool cached{};
    bool evicted{};
    std::size_t peers{};
    std::size_t queued{};
};

[[nodiscard]] bool same_endpoint(const state::gameplay::Endpoint& left,
                                 const state::gameplay::Endpoint& right) noexcept {
    return left.address == right.address && left.port == right.port;
}

[[nodiscard]] state::gameplay::PeerLink*
find_locked(const state::gameplay::Endpoint& from) noexcept {
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage != state::gameplay::PeerStage::absent
            && same_endpoint(peer.endpoint, from)) {
            return &peer;
        }
    }
    return nullptr;
}

[[nodiscard]] bool carries_locked(const state::gameplay::PeerLink& peer,
                                  std::uint64_t sessionId) noexcept {
    for (const std::uint64_t held : peer.sessions) {
        if (held == sessionId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool delivered_locked(const CachedPlayerProperties& publication,
                                    const state::gameplay::Endpoint& endpoint) noexcept {
    return std::any_of(publication.delivered.begin(),
                       publication.delivered.begin() + publication.deliveredCount,
                       [&endpoint](const state::gameplay::Endpoint& delivered) noexcept {
                           return same_endpoint(delivered, endpoint);
                       });
}

void mark_delivered_locked(CachedPlayerProperties& publication,
                           const state::gameplay::Endpoint& endpoint) noexcept {
    if (delivered_locked(publication, endpoint)
        || publication.deliveredCount >= publication.delivered.size()) {
        return;
    }
    publication.delivered[publication.deliveredCount++] = endpoint;
}

// reliable queue acceptance, not receipt, is the handoff: after it the queue resends the exact
// fragments until the peer acknowledges
void clear_delivery_locked(const state::gameplay::Endpoint& endpoint,
                           std::uint64_t sessionId = 0) noexcept {
    for (CachedPlayerProperties& publication : g_playerPropertiesCache) {
        if (!publication.occupied
            || (sessionId != 0 && publication.sessionId != sessionId)) {
            continue;
        }
        for (std::size_t index = 0; index < publication.deliveredCount;) {
            if (!same_endpoint(publication.delivered[index], endpoint)) {
                ++index;
                continue;
            }
            for (std::size_t following = index + 1; following < publication.deliveredCount;
                 ++following) {
                publication.delivered[following - 1] = publication.delivered[following];
            }
            publication.delivered[publication.deliveredCount - 1] = {};
            --publication.deliveredCount;
        }
    }
}

void discard_source_publications_locked(const state::gameplay::Endpoint& endpoint,
                                        std::uint64_t sessionId = 0) noexcept {
    for (CachedPlayerProperties& publication : g_playerPropertiesCache) {
        if (publication.occupied && same_endpoint(publication.source, endpoint)
            && (sessionId == 0 || publication.sessionId == sessionId)) {
            publication = {};
        }
    }
}

[[nodiscard]] CachedPlayerProperties*
cache_player_properties_locked(const state::gameplay::Endpoint& source,
                               std::uint64_t sessionId,
                               std::uint32_t declaredSize,
                               std::span<const std::byte> body,
                               std::size_t bodyBits,
                               bool& evicted) noexcept {
    evicted = false;
    if (sessionId == 0 || body.empty() || body.size() > kPlayerPropertiesBodyCapacity
        || bodyBits < sizeof(std::uint64_t) * kByteBits || bodyBits > body.size() * kByteBits) {
        return nullptr;
    }

    CachedPlayerProperties* destination = nullptr;
    for (CachedPlayerProperties& publication : g_playerPropertiesCache) {
        if (!publication.occupied) {
            destination = &publication;
            break;
        }
    }
    if (destination == nullptr) {
        destination = &g_playerPropertiesCache.front();
        for (CachedPlayerProperties& publication : g_playerPropertiesCache) {
            if (publication.sequence < destination->sequence) {
                destination = &publication;
            }
        }
        evicted = true;
    }

    std::uint64_t sequence = ++g_playerPropertiesSequence;
    if (sequence == 0) {
        sequence = ++g_playerPropertiesSequence;
    }
    *destination = {};
    std::copy(body.begin(), body.end(), destination->body.begin());
    destination->source = source;
    destination->sessionId = sessionId;
    destination->sequence = sequence;
    destination->declaredSize = declaredSize;
    destination->bodyBits = bodyBits;
    destination->bodyBytes = body.size();
    destination->occupied = true;
    return destination;
}

[[nodiscard]] std::size_t
replay_player_properties_locked(state::gameplay::PeerLink& peer,
                                std::span<const std::uint64_t> authorizedSessions,
                                std::size_t maximum) noexcept {
    if (!peer.applicationReady || authorizedSessions.empty() || maximum == 0) {
        return 0;
    }
    std::array<CachedPlayerProperties*, kPlayerPropertiesCacheCapacity> pending{};
    std::size_t pendingCount = 0;
    for (CachedPlayerProperties& publication : g_playerPropertiesCache) {
        const bool authorized =
            std::find(authorizedSessions.begin(), authorizedSessions.end(), publication.sessionId)
            != authorizedSessions.end();
        // an ID-37 describes the player on the sending channel; echoing it back makes the sender
        // bind its own profile to the remote row. Keep the source ineligible, relay to others only
        const bool remoteSource = !same_endpoint(publication.source, peer.endpoint);
        if (publication.occupied && authorized && remoteSource
            && carries_locked(peer, publication.sessionId)
            && !delivered_locked(publication, peer.endpoint)) {
            pending[pendingCount++] = &publication;
        }
    }
    std::sort(pending.begin(),
              pending.begin() + pendingCount,
              [](const CachedPlayerProperties* left,
                 const CachedPlayerProperties* right) noexcept {
                  return left->sequence < right->sequence;
              });

    std::size_t replayed = 0;
    for (std::size_t index = 0; index < pendingCount && replayed < maximum; ++index) {
        CachedPlayerProperties& publication = *pending[index];
        if (!wire::enqueue_message(
                peer.outbound,
                kPlayerPropertiesId,
                publication.declaredSize,
                std::span<const std::byte>(publication.body).first(publication.bodyBytes),
                publication.bodyBits)) {
            break;
        }
        mark_delivered_locked(publication, peer.endpoint);
        peer.acknowledgementOwed = true;
        report(core::log::Level::debug,
               "ev=gameplay stage=player_properties_route result=queued "
               "session=0x%016llX sequence=%llu source=0x%08X:%u target=0x%08X:%u",
               static_cast<unsigned long long>(publication.sessionId),
               static_cast<unsigned long long>(publication.sequence),
               publication.source.address,
               static_cast<unsigned>(publication.source.port),
               peer.endpoint.address,
               static_cast<unsigned>(peer.endpoint.port));
        ++replayed;
    }
    return replayed;
}

[[nodiscard]] PlayerPropertiesRelayResult
relay_player_properties(const state::gameplay::Endpoint& from,
                        const wire::AssembledMessage& message) noexcept {
    PlayerPropertiesRelayResult result{};
    if (message.id != kPlayerPropertiesId
        || message.declaredSize != kPlayerPropertiesDeclaredSize
        || message.bitCount < message.bodyBitOffset
        || message.bodyBitOffset % kByteBits != 0) {
        return result;
    }

    const std::size_t bodyBits = message.bitCount - message.bodyBitOffset;
    const std::size_t bodyBytes = (bodyBits + kByteBits - 1U) / kByteBits;
    const std::size_t bodyByteOffset = message.bodyBitOffset / kByteBits;
    if (bodyBits < sizeof(std::uint64_t) * kByteBits
        || bodyByteOffset + bodyBytes > message.bytes.size()) {
        return result;
    }

    const std::span<const std::byte> body{message.bytes.data() + bodyByteOffset, bodyBytes};
    bits::Reader reader(body);
    if (!bits::read_raw_u64(reader, result.sessionId) || result.sessionId == 0) {
        result.sessionId = 0;
        return result;
    }

    // takes the group lock, so it must finish before this transport takes g_lock; carrying a session
    // isn't enough, only its admitted player-owning endpoint may publish the profile stream
    if (!group::authorizes_player_publication(from, result.sessionId)) {
        return result;
    }

    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* const source = find_locked(from);
    if (source == nullptr || !source->applicationReady
        || !carries_locked(*source, result.sessionId)) {
        ReleaseSRWLockExclusive(&g_lock);
        return result;
    }
    result.sourceAuthorized = true;
    if (!kOpaquePlayerPropertiesRelayEnabled) {
        result.suppressed = true;
        ReleaseSRWLockExclusive(&g_lock);
        return result;
    }
    CachedPlayerProperties* const publication = cache_player_properties_locked(
        from,
        result.sessionId,
        message.declaredSize,
        body,
        bodyBits,
        result.evicted);
    if (publication == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return result;
    }
    result.cached = true;
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage == state::gameplay::PeerStage::absent || !peer.applicationReady
            || same_endpoint(peer.endpoint, from)
            || !carries_locked(peer, result.sessionId)) {
            continue;
        }
        ++result.peers;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

[[nodiscard]] state::gameplay::PeerLink* find_session_locked(std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return nullptr;
    }
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage != state::gameplay::PeerStage::absent && carries_locked(peer, sessionId)) {
            return &peer;
        }
    }
    return nullptr;
}

[[nodiscard]] state::gameplay::PeerLink*
find_endpoint_session_locked(const state::gameplay::Endpoint& endpoint,
                             std::uint64_t sessionId) noexcept {
    state::gameplay::PeerLink* const peer = find_locked(endpoint);
    return peer != nullptr && sessionId != 0 && carries_locked(*peer, sessionId) ? peer : nullptr;
}

[[nodiscard]] std::uint64_t sole_session_locked(const state::gameplay::PeerLink& peer) noexcept {
    std::uint64_t only = 0;
    for (const std::uint64_t held : peer.sessions) {
        if (held == 0) {
            continue;
        }
        if (only != 0) {
            return 0;
        }
        only = held;
    }
    return only;
}

[[nodiscard]] std::uint64_t session_for_endpoint(const state::gameplay::Endpoint& from) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* const peer = find_locked(from);
    const std::uint64_t only = peer == nullptr ? 0 : sole_session_locked(*peer);
    ReleaseSRWLockShared(&g_lock);
    return only;
}

[[nodiscard]] state::gameplay::PeerLink* allocate_locked() noexcept {
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage == state::gameplay::PeerStage::absent) {
            return &peer;
        }
    }
    return nullptr;
}

void local_address(std::array<std::byte, wire::kAddressBlobSize>& output) noexcept {
    const state::gameplay::Endpoint advertised = endpoint::advertised();
    middleware::gameplay::descriptor::write_net_addr(advertised.address, advertised.port, output);
}

[[nodiscard]] std::uint32_t random_sequence() noexcept {
    std::array<std::byte, sizeof(std::uint32_t)> bytes{};
    if (!middleware::crypto::random::fill(bytes)) {
        return 0;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[index]) << (index * kByteBits);
    }
    return value;
}

void answer_establish(const state::gameplay::Endpoint& to,
                      std::uint32_t remoteChannelId,
                      const wire::ConnectEstablish& body) noexcept {
    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    std::size_t size = 0;
    if (!wire::write_establish(writer, body) || !writer.finish(size)) {
        report(core::log::Level::warn, "ev=gameplay stage=establish result=fail reason=encode");
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_locked(to);
    const bool queued =
        peer != nullptr && peer->remoteConnectionSequence == remoteChannelId
        && wire::enqueue_message(peer->outbound,
                                 static_cast<std::uint8_t>(wire::ConnectId::establish),
                                 wire::kEstablishSize,
                                 {buffer.data(), size},
                                 writer.bit_count());
    if (queued) {
        peer->acknowledgementOwed = true;
        peer->outbound.awaitingAcknowledgement = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
    report(core::log::Level::info,
           "ev=gameplay stage=establish result=%s local=0x%08X remote=0x%08X",
           queued ? "queued" : "fail",
           body.channelId,
           body.remoteChannelId);
}

void answer_connect(const state::gameplay::Endpoint& from,
                    const wire::ConnectRequest& request,
                    std::uint64_t now) noexcept {
    wire::ConnectResponse response{};
    response.remoteChannelId = request.channelId;
    response.remoteSequence = request.sequence;
    local_address(response.address);

    AcquireSRWLockExclusive(&g_lock);
    // keyed by endpoint: the client holds one channel per host peer, so a second link would stamp
    // packets with a channel id it already retired
    state::gameplay::PeerLink* peer = find_locked(from);
    // a repeat of the same request is a retransmission; a different channel or sequence is a new
    // incarnation the peer built without announcing the teardown
    const bool rebuilt = peer != nullptr
                         && (peer->remoteConnectionSequence != request.channelId
                             || peer->remoteTransportSequence != request.sequence);
    if (peer == nullptr) {
        peer = allocate_locked();
    }
    const bool fresh =
        peer != nullptr && (peer->stage == state::gameplay::PeerStage::absent || rebuilt);
    if (fresh) {
        // delivery marks belong to a channel incarnation, so a rebuilt channel must get the cached profile stream again
        clear_delivery_locked(from);
        // sessions outlive the channel: the client rebuilds one channel under every group session and
        // rejoins none, so dropping them here strands each one
        const std::array<std::uint64_t, state::gameplay::kSessionsPerLink> held =
            peer->stage == state::gameplay::PeerStage::absent
                ? std::array<std::uint64_t, state::gameplay::kSessionsPerLink>{}
                : peer->sessions;
        *peer = {};
        g_ackContexts[static_cast<std::size_t>(peer - g_peers.data())] = {};
        g_nativeStamps[static_cast<std::size_t>(peer - g_peers.data())] = {
            std::bit_cast<std::int32_t>(request.sequence - 1U), true};
        g_issuedOrdinals[static_cast<std::size_t>(peer - g_peers.data())] = 0;
        peer->sessions = held;
        peer->endpoint = from;
        // the channel id is an incarnation counter: the peer refuses one that doesn't increase and reads all ones as unset
        peer->localConnectionSequence = ++g_channelId;
        // the peer builds its receive window from the announced sequence and expects the first packet one
        // past it, so announce the sequence before the first packet, not the packet itself
        peer->localTransportSequence =
            (random_sequence() & ~static_cast<std::uint32_t>(kPacketSequenceModulus - 1))
            | static_cast<std::uint32_t>(kFirstPacketSequence - 1);
    }
    if (peer != nullptr) {
        peer->remoteConnectionSequence = request.channelId;
        peer->remoteTransportSequence = request.sequence;
        if (fresh) {
            static_cast<void>(g_fragments[static_cast<std::size_t>(peer - g_peers.data())].reset(
                connection_incarnation(*peer),
                static_cast<std::uint8_t>(peer->remoteConnectionSequence % kSequenceGuardBase),
                fragment_part_bytes()));
        }
        peer->remoteAddress = request.address;
        peer->remoteAddressPresent = true;
        if (fresh) {
            peer->stage = state::gameplay::PeerStage::connecting;
        }
        peer->lastTick = now;
        response.channelId = peer->localConnectionSequence;
        response.sequence = peer->localTransportSequence;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (peer == nullptr) {
        report(core::log::Level::warn, "ev=gameplay stage=connect result=fail reason=capacity limit=%zu", kTransportPeerCapacity);
        return;
    }

    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    wire::MessageHeader header{static_cast<std::uint8_t>(wire::ConnectId::response),
                               wire::kResponseSize};
    std::size_t size = 0;
    if (!wire::open_container(writer) || !wire::write_header(writer, header)
        || !wire::write_response(writer, response) || !wire::close_container(writer)
        || !writer.finish(size) || !send_transport(from, {buffer.data(), size})) {
        report(core::log::Level::warn, "ev=gameplay stage=connect result=fail reason=send");
        return;
    }
    report(core::log::Level::info,
           "ev=gameplay stage=connect result=ok peer=%u local=0x%08X remote=0x%08X rebuilt=%u receive_start=%u send_start=%u",
           from.port,
           response.channelId,
           request.channelId,
           rebuilt ? 1U : 0U, request.sequence, response.sequence);
    // the peer refuses any first reliable record that isn't a connect establish, so enqueue this first
    wire::ConnectEstablish establish{};
    establish.remoteChannelId = response.remoteChannelId;
    establish.channelId = response.channelId;
    answer_establish(from, request.channelId, establish);
}

[[nodiscard]] bool bind_session(const state::gameplay::Endpoint& from,
                                std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    // a join for a second region arrives on the same channel as the first, out of band while that
    // channel is still being rebuilt
    state::gameplay::PeerLink* const peer = find_locked(from);
    const char* result = "nolink";
    bool bound = false;
    std::uint32_t channel = 0;
    if (peer != nullptr) {
        channel = peer->localConnectionSequence;
        result = "full";
        for (std::uint64_t& slot : peer->sessions) {
            if (slot == sessionId) {
                result = "held";
                bound = true;
                break;
            }
            if (slot == 0) {
                slot = sessionId;
                result = "bound";
                bound = true;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    report(bound ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=link result=%s session=0x%016llX peer=%u local=0x%08X",
           result,
           static_cast<unsigned long long>(sessionId),
           from.port,
           channel);
    return bound;
}

void answer_join(const state::gameplay::Endpoint& from, const wire::JoinRequest& request,
                 const wire::JoinMembers* nativeMembers) noexcept {
    const std::uint64_t hostSession = endpoint::identity().onlineSessionId;
    wire::RefuseReason reason = wire::RefuseReason::notFound;
    if (wire::admit(request, hostSession, reason)) {
        const bool bound = bind_session(from, request.sessionId);
        // the peer acks the packet that finishes establishment before dispatching application records;
        // publishing into that window skips their reliable sequences, so wait for the next connected packet
        const bool ready = bound && application_ready_for(from, request.sessionId);
        const bool published =
            ready && group::publish_membership(from, request.joinId, request.sessionId,nativeMembers);
        // the peer needs both before it finishes: the snapshot names it and the parameter update releases
        // the latch its tick waits on
        const bool parameters = ready && group::publish_join_parameters(from, request.sessionId);
        report(core::log::Level::info,
               "ev=gameplay stage=join result=admit build=%u..%u exe=%u session=0x%016llX "
               "host=0x%016llX join=0x%016llX membership=%s parameters=%s",
               request.minimumBuild,
               request.maximumBuild,
               static_cast<unsigned>(request.executableType),
               static_cast<unsigned long long>(request.sessionId),
               static_cast<unsigned long long>(hostSession),
               static_cast<unsigned long long>(request.joinId),
               published ? "queued" : ready ? "fail" : "deferred",
               parameters ? "queued" : ready ? "fail" : "deferred");
        return;
    }
    if (!wire::answerable(request)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=join result=drop reason=protocol value=0x%04X",
               static_cast<unsigned>(request.protocolVersion));
        return;
    }
    report(core::log::Level::warn,
           "ev=gameplay stage=join result=refuse reason=%u build=%u..%u exe=%u session=0x%016llX "
           "host=0x%016llX",
           static_cast<unsigned>(reason),
           request.minimumBuild,
           request.maximumBuild,
           static_cast<unsigned>(request.executableType),
           static_cast<unsigned long long>(request.sessionId),
           static_cast<unsigned long long>(hostSession));
    wire::JoinRefuse refusal{};
    refusal.sessionId = request.sessionId;
    refusal.joinId = request.joinId;
    refusal.reason = reason;

    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    const wire::MessageHeader header{static_cast<std::uint8_t>(wire::JoinId::refuse),
                                     wire::kJoinRefuseSize};
    std::size_t size = 0;
    if (!wire::open_container(writer) || !wire::write_header(writer, header)
        || !wire::write_join_refuse(writer, refusal) || !wire::close_container(writer)
        || !writer.finish(size) || !send_transport(from, {buffer.data(), size})) {
        report(core::log::Level::warn, "ev=gameplay stage=join result=fail reason=send");
        return;
    }
    report(core::log::Level::info,
           "ev=gameplay stage=join result=refuse reason=%u",
           static_cast<unsigned>(refusal.reason));
}

void consume_container(const state::gameplay::Endpoint& from,
                       std::span<const std::byte> payload,
                       std::uint64_t now) noexcept {
    bits::Reader reader(payload);
    if (!wire::read_marker(reader)) {
        return;
    }
    for (;;) {
        wire::MessageHeader header{};
        bool present = false;
        if (!wire::read_header(reader, header, present)) {
            report(core::log::Level::debug, "ev=gameplay stage=oob result=drop reason=header");
            return;
        }
        if (!present) {
            return;
        }
        if (header.id == static_cast<std::uint8_t>(wire::ConnectId::request)) {
            wire::ConnectRequest request{};
            if (!wire::read_request(reader, request)) {
                return;
            }
            answer_connect(from, request, now);
            continue;
        }
        if (header.id == static_cast<std::uint8_t>(wire::JoinId::request)) {
            wire::JoinRequest request{};
            if (wire::read_join_request(reader, request)) {
                wire::JoinMembers members{};
                const bool decoded=wire::read_join_members(reader,members);
                answer_join(from, request,decoded?&members:nullptr);
            }
            // the optional session descriptor after the identity tables stays unparsed; prefix-only requests
            // keep legacy admission, shared identity stays fail-closed
            return;
        }
        if (header.id == static_cast<std::uint8_t>(wire::ConnectId::closed)) {
            wire::ConnectEnd closed{};
            if (!wire::read_closed(reader, closed)) {
                return;
            }
            // an association can close during a normal native session rebuild; tear down only the gameplay row,
            // the realtime lobby changes only through its own leave/boot lifecycle
            group::release_endpoint(from);
            drop_endpoint(from);
            report(core::log::Level::info,
                   "ev=gameplay stage=peer result=closed reason=%u",
                   static_cast<unsigned>(closed.reason));
            return;
        }
        if (group::consume(from, session_for_endpoint(from), header.id, reader, now)) {
            continue;
        }
        // an undecoded message ends the chain: its body width is unknown, so anything behind it would be
        // read at the wrong offset
        report(core::log::Level::debug, "ev=gameplay stage=oob result=stop id=%u", header.id);
        return;
    }
}

void apply_message(state::gameplay::PeerLink& peer,
                   const wire::AssembledMessage& message) noexcept {
    if (message.id == static_cast<std::uint8_t>(wire::ConnectId::establish)
        && peer.stage == state::gameplay::PeerStage::connecting) {
        peer.stage = state::gameplay::PeerStage::connected;
    }
}

std::size_t apply_acknowledgement(state::gameplay::PeerLink& peer,
                                  const wire::AckState& ack) noexcept {
    const auto slot=static_cast<std::size_t>(&peer-g_peers.data());
    std::uint16_t resolvedBase = 0;
    if (!ack.ringInitialized || !peer.outboundHeadPresent
        || !wire::resolve_ack_context(g_ackContexts[slot],
                                     ack.outboundHeadPresent, ack.outboundHead, ack.receiveHead,
                                     g_issuedOrdinals[slot], resolvedBase)) {
        return 0;
    }
    if(!wire::prefix_confirmed(peer.outbound,ack,peer.outboundHead,resolvedBase))return 0;
    const std::size_t retired = peer.outbound.sentCount;
    if (retired == 0 || retired > peer.outbound.count) {
        return 0;
    }
    // retire only the prefix this packet carried; fragments that didn't fit stay in sequence order,
    // matching the retail queue's per-fragment flight tracking
    const std::size_t remaining = peer.outbound.count - retired;
    for (std::size_t index = 0; index < remaining; ++index) {
        peer.outbound.fragments[index] = peer.outbound.fragments[index + retired];
    }
    for (std::size_t index = remaining; index < peer.outbound.count; ++index) {
        peer.outbound.fragments[index] = {};
    }
    peer.outbound.count = remaining;
    peer.outbound.sentCount = 0;
    peer.outbound.awaitingAcknowledgement = false;
    peer.outbound.carryingCount = 0;
    peer.outbound.carryingPackets = {};
    return retired;
}

void consume_established(const state::gameplay::Endpoint& from,
                         std::span<const std::byte> payload,
                         std::uint64_t now,
                         std::uint64_t requiredIncarnation = 0) noexcept {
    wire::EstablishedPacket packet{};
    if (!wire::decode_established(payload, false, packet)) {
        report(core::log::Level::debug, "ev=gameplay stage=packet result=drop reason=grammar");
        return;
    }
    std::array<std::uint8_t, kMessageReportCapacity> delivered{};
    std::size_t deliveredCount = 0;
    unsigned stage = 0;
    std::size_t queueRetired = 0;
    std::size_t queueRemaining = 0;
    std::uint16_t clearedPacket = 0;
    std::uint64_t sessionId = 0;
    // the reliable window never resynchronises, so a stalled queue only shows as a refused record
    // against the sequence it's still waiting for
    std::size_t largeDropped = 0;
    std::uint16_t largeNext = 0;
    std::uint16_t largeFirst = 0;
    bool externalProbeReport = false;
    bool externalProbeDecoded = false;
    bool postQueueBitZero = false;
    bool postQueueBitOne = false;
    std::uint32_t externalProbeOrdinal = 0;
    std::uint32_t externalPresentOrdinal = 0;
    std::uint8_t externalViewKind = 0;
    std::uint8_t externalViewListCount = 0;
    std::size_t externalTailBits = 0;
    std::uint8_t externalPrefixABits = 0;
    std::uint8_t externalPrefixBBits = 0;
    std::uint64_t externalPrefixA = 0;
    std::uint64_t externalPrefixB = 0;
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_locked(from);
    if (requiredIncarnation && (!peer || connection_incarnation(*peer) != requiredIncarnation)) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (peer && packet.connectionSequenceLow2 != peer->remoteConnectionSequence % kSequenceGuardBase) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::debug, "ev=gameplay stage=packet result=drop reason=connection_guard");
        return;
    }
    const auto tailResult = wire::validate_absent_external_tail(payload, packet.externalBitOffset);
    std::array<wire::AssembledMessage, kMessageReportCapacity> bodies{};
    if (peer != nullptr) {
        const bool wasConnected = peer->stage == state::gameplay::PeerStage::connected;
        sessionId = sole_session_locked(*peer);
        // native entity state is retired by this packet's ACK; until the external decoder commits it,
        // process reliable records but keep the packet unconfirmed
        auto& nativeStamp = g_nativeStamps[static_cast<std::size_t>(peer - g_peers.data())];
        wire::NativeStampResult stagedStamp{};
        if (tailResult == wire::ExternalTailResult::accepted_absent
            && wire::native_packet_stamp(nativeStamp, packet.ack.outboundHead,
                packet.ack.outboundHeadPresent, packet.ack.headMinusCursor, stagedStamp)) {
            nativeStamp.priorHead = stagedStamp.nextHead;
            if (packet.ack.outboundHeadPresent)
                wire::record_accepted_packet(*peer, packet.ack.outboundHead);
        }
        clearedPacket = peer->outbound.sentInPacket;
        queueRetired = apply_acknowledgement(*peer, packet.ack);
        queueRemaining = peer->outbound.count;
        peer->acknowledgementOwed = true;
        peer->lastTick = now;
        largeDropped = wire::accept_records(packet.large, peer->large);
        largeNext = peer->large.nextSequence;
        largeFirst = packet.large.count == 0 ? 0 : packet.large.records[0].sequence;
        wire::accept_records(packet.small, peer->small);
        wire::AssembledMessage message{};
        while (wire::drain_message(peer->large, message)) {
            apply_message(*peer, message);
            if (deliveredCount < delivered.size()) {
                delivered[deliveredCount] = message.id;
                bodies[deliveredCount] = message;
                ++deliveredCount;
            }
        }
        while (wire::drain_message(peer->small, message)) {
            apply_message(*peer, message);
            if (deliveredCount < delivered.size()) {
                delivered[deliveredCount] = message.id;
                bodies[deliveredCount] = message;
                ++deliveredCount;
            }
        }
        // don't release application traffic on the packet that changes the stage; its ack completes the
        // remote side's establishment only after this exchange
        if (wasConnected) {
            peer->applicationReady = true;
        }
        if (peer->stage == state::gameplay::PeerStage::connected
            && packet.externalBitOffset <= payload.size() * kByteBits) {
            externalProbeOrdinal = ++peer->postQueueProbePackets;
            externalViewKind = peer->view.kind;
            externalViewListCount = peer->view.listCount;
            externalTailBits = payload.size() * kByteBits - packet.externalBitOffset;

            bits::Reader externalReader(payload);
            std::uint64_t extendedPresent = 0;
            std::uint64_t bodyPresent = 0;
            externalProbeDecoded = externalReader.skip(packet.externalBitOffset)
                                   && externalReader.read(1, extendedPresent)
                                   && externalReader.read(1, bodyPresent);
            if (externalProbeDecoded) {
                postQueueBitZero = extendedPresent != 0;
                postQueueBitOne = bodyPresent != 0;
                if (postQueueBitOne) {
                    externalPresentOrdinal = ++peer->postQueueSecondBitPackets;
                }
                externalPrefixABits = static_cast<std::uint8_t>((std::min)(
                    externalReader.remaining_bits(),
                    static_cast<std::size_t>(kExternalProbeWordBits)));
                if (externalPrefixABits != 0) {
                    externalProbeDecoded =
                        externalReader.read(externalPrefixABits, externalPrefixA);
                }
                externalPrefixBBits = static_cast<std::uint8_t>((std::min)(
                    externalReader.remaining_bits(),
                    static_cast<std::size_t>(kExternalProbeWordBits)));
                if (externalProbeDecoded && externalPrefixBBits != 0) {
                    externalProbeDecoded =
                        externalReader.read(externalPrefixBBits, externalPrefixB);
                }
            }
            externalProbeReport = externalProbeOrdinal <= 8
                                  || is_power_of_two(externalProbeOrdinal)
                                  || (postQueueBitOne
                                      && (externalPresentOrdinal <= 32
                                          || is_power_of_two(externalPresentOrdinal)));
        }
        stage = static_cast<unsigned>(peer->stage);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (peer == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < deliveredCount; ++index) {
        report(core::log::Level::info,
               "ev=gameplay stage=message result=ok id=%u peerstage=%u",
               static_cast<unsigned>(delivered[index]),
               stage);
        // the connect establish belongs to this layer and apply_message already took it; passing it on
        // would report it as undecoded on every connection
        const wire::AssembledMessage& body = bodies[index];
        if (body.id == static_cast<std::uint8_t>(wire::ConnectId::establish)) {
            continue;
        }
        if (body.bitCount < body.bodyBitOffset) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=message result=invalid_body_boundary id=%u bits=%zu "
                   "offset=%zu",
                   static_cast<unsigned>(body.id),
                   body.bitCount,
                   body.bodyBitOffset);
            continue;
        }
        capture_parameter_request_message(from, body);
        report_session_publication_message(body);
        if (body.id == kPlayerPropertiesId) {
            const PlayerPropertiesRelayResult relay = relay_player_properties(from, body);
            const std::size_t bodyBits = body.bitCount - body.bodyBitOffset;
            const bool accepted = relay.sessionId != 0 && relay.sourceAuthorized
                                  && (relay.suppressed || relay.cached);
            const char* const relayResult =
                relay.sessionId == 0                 ? "invalid"
                : !relay.sourceAuthorized            ? "unauthorized"
                : relay.suppressed                   ? "suppressed_no_provenance"
                : !relay.cached                      ? "cache_rejected"
                : relay.queued == relay.peers        ? "relayed"
                                                     : "retry_armed";
            report(accepted && (relay.suppressed || !relay.evicted) ? core::log::Level::info
                                                                     : core::log::Level::warn,
                   "ev=gameplay stage=player_properties result=%s session=0x%016llX "
                   "peers=%zu queued=%zu declared=%u body_bits=%zu cached=%u evicted=%u",
                   relayResult,
                   static_cast<unsigned long long>(relay.sessionId),
                   relay.peers,
                   relay.queued,
                   static_cast<unsigned>(body.declaredSize),
                   bodyBits,
                   static_cast<unsigned>(relay.cached ? 1U : 0U),
                   static_cast<unsigned>(relay.evicted ? 1U : 0U));
        }
        if (body.id == group_wire::kParameterRequestId
            && body.declaredSize != group_wire::kParameterRequestSize) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=parameter_authority result=declared_size_rejected "
                   "declared=%u expected=%u",
                   static_cast<unsigned>(body.declaredSize),
                   static_cast<unsigned>(group_wire::kParameterRequestSize));
            continue;
        }
        if (body.id == group_wire::kBootMachineId
            && body.declaredSize != group_wire::kBootMachineSize) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=fireteam_boot result=declared_size_rejected "
                   "declared=%u expected=%u",
                   static_cast<unsigned>(body.declaredSize),
                   static_cast<unsigned>(group_wire::kBootMachineSize));
            continue;
        }
        const std::size_t messageByteCount = (body.bitCount + kByteBits - 1) / kByteBits;
        bits::Reader reader({body.bytes.data(), messageByteCount}, body.bitCount);
        const std::size_t bodyBitCount = body.bitCount - body.bodyBitOffset;
        if (reader.skip(body.bodyBitOffset)
            && !group::consume(from, sessionId, body.id, reader, now, bodyBitCount)) {
            report(core::log::Level::debug,
                   "ev=gameplay stage=message result=undecoded id=%u",
                   static_cast<unsigned>(body.id));
        }
    }
    if (queueRetired != 0) {
        report(core::log::Level::info,
               "ev=gameplay stage=sendqueue result=retired packet=%u fragments=%zu remaining=%zu "
               "base=%u entries=%u",
               static_cast<unsigned>(clearedPacket),
               queueRetired,
               queueRemaining,
               static_cast<unsigned>(packet.ack.receiveHead),
               static_cast<unsigned>(packet.ack.reportedCount));
    }
    if (externalProbeReport) {
        report(externalProbeDecoded ? core::log::Level::info : core::log::Level::warn,
               "ev=gameplay stage=post_queue_probe result=%s sample=%u bit1_sample=%u "
               "session=0x%016llX view_kind=%u view_list=%u payload_bytes=%zu "
               "tail_offset=%zu tail_bits=%zu bit0=%u bit1=%u "
               "prefix_a_bits=%u prefix_a=0x%016llX prefix_b_bits=%u prefix_b=0x%016llX",
               externalProbeDecoded ? "observed" : "grammar",
               externalProbeOrdinal,
               externalPresentOrdinal,
               static_cast<unsigned long long>(sessionId),
               static_cast<unsigned>(externalViewKind),
               static_cast<unsigned>(externalViewListCount),
               payload.size(),
               packet.externalBitOffset,
               externalTailBits,
               static_cast<unsigned>(postQueueBitZero ? 1U : 0U),
               static_cast<unsigned>(postQueueBitOne ? 1U : 0U),
               static_cast<unsigned>(externalPrefixABits),
               static_cast<unsigned long long>(externalPrefixA),
               static_cast<unsigned>(externalPrefixBBits),
               static_cast<unsigned long long>(externalPrefixB));
    }
    report(core::log::Level::debug,
           "ev=gameplay stage=packet result=ok seq=%u cursor=%u base=%u entries=%u large=%u "
           "small=%u first=%u next=%u drop=%zu",
           static_cast<unsigned>(packet.ack.outboundHead),
           static_cast<unsigned>(packet.ack.headMinusCursor),
           static_cast<unsigned>(packet.ack.receiveHead),
           static_cast<unsigned>(packet.ack.reportedCount),
           static_cast<unsigned>(packet.large.count),
           static_cast<unsigned>(packet.small.count),
           static_cast<unsigned>(largeFirst),
           static_cast<unsigned>(largeNext),
           largeDropped);
}

void consume_fragment(const state::gameplay::Endpoint& from,
                      std::span<const std::byte> payload, std::uint64_t now) noexcept {
    std::array<std::byte, wire::fragments::kMaximumPacketBytes> complete{};
    std::size_t size = 0;
    std::uint64_t incarnation = 0;
    auto result = wire::fragments::Result::refused;
    AcquireSRWLockExclusive(&g_lock);
    const auto* peer = find_locked(from);
    if (peer) {
        incarnation = connection_incarnation(*peer);
        const auto receipt = g_fragments[static_cast<std::size_t>(peer - g_peers.data())]
                                 .receive(incarnation, payload, now);
        result = receipt.result;
        if (result == wire::fragments::Result::complete
            || (result == wire::fragments::Result::duplicate && !receipt.packet.empty())) {
            size = receipt.packet.size();
            std::copy(receipt.packet.begin(), receipt.packet.end(), complete.begin());
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (size) consume_established(from, std::span(complete).first(size), now, incarnation);
    else if (result != wire::fragments::Result::pending && result != wire::fragments::Result::duplicate)
        report(core::log::Level::debug, "ev=gameplay stage=fragment result=refused code=%u",
               static_cast<unsigned>(result));
}

[[nodiscard]] bool send_acknowledgement(const state::gameplay::PeerLink& peer) noexcept {
    wire::AckState ack{};
    ack.outboundHead = peer.outboundHead;
    ack.outboundHeadPresent = peer.outboundHeadPresent;
    // the peer subtracts this from the decoded sequence to place its receive window; zero collapses it
    // and the peer discards every packet. Captures use one even as the head advances, so mirror that
    ack.headMinusCursor = kMinimumHeadCursor;
    ack.receiveHead = peer.receiveHead;
    ack.ringInitialized = peer.ringInitialized;
    ack.received = peer.received;
    ack.delay = kDelaySentinel;

    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    const auto guard = static_cast<std::uint8_t>(peer.localConnectionSequence % kSequenceGuardBase);
    std::size_t size = 0;
    const std::size_t fragmentCount = peer.outbound.awaitingAcknowledgement
                                          ? peer.outbound.sentCount
                                          : 0;
    if (!wire::write_head_and_ack(writer, guard, ack)
        || !wire::write_queue(writer, peer.outbound, fragmentCount)
        || !wire::write_empty_queue(writer) || !wire::write_absent_filler(writer)
        || !writer.finish(size)) {
        return false;
    }
    const bool sent = send_transport(peer.endpoint, {buffer.data(), size});
    const std::uint16_t first =
        fragmentCount == 0 ? 0 : peer.outbound.fragments[0].sequence;
    const std::uint16_t last = fragmentCount == 0
                                   ? 0
                                   : peer.outbound.fragments[fragmentCount - 1].sequence;
    report(core::log::Level::debug,
           "ev=gameplay stage=outbound result=%s head=%u cursor=%u "
           "fragments=%zu first=%u last=%u bytes=%zu",
           sent ? "sent" : "fail",
           static_cast<unsigned>(peer.outboundHead),
           static_cast<unsigned>(ack.headMinusCursor),
           fragmentCount,
           static_cast<unsigned>(first),
           static_cast<unsigned>(last),
           size);
    return sent;
}

} // namespace

void deliver(const state::gameplay::Endpoint& from,
             std::span<const std::byte> payload,
             std::uint64_t now) noexcept {
    if (payload.empty()) {
        return;
    }
    if ((std::to_integer<unsigned>(payload[0]) >> kMarkerShift) != 0) {
        consume_container(from, payload, now);
        return;
    }
    if ((std::to_integer<unsigned>(payload[0]) & 0x40U) != 0)
        consume_fragment(from, payload, now);
    else consume_established(from, payload, now);
}

bool send_container(const state::gameplay::Endpoint& to,
                    std::uint8_t id,
                    std::uint32_t declaredSize,
                    std::span<const std::byte> body,
                    std::size_t bodyBits) noexcept {
    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    const wire::MessageHeader header{id, declaredSize};
    if (!wire::open_container(writer) || !wire::write_header(writer, header)) {
        return false;
    }
    bits::Reader reader(body);
    std::size_t remaining = bodyBits;
    while (remaining != 0) {
        const auto width = static_cast<std::uint8_t>(remaining < kByteBits ? remaining : kByteBits);
        std::uint64_t value = 0;
        if (!reader.read(width, value) || !writer.write(value, width)) {
            return false;
        }
        remaining -= width;
    }
    std::size_t size = 0;
    if (!wire::close_container(writer) || !writer.finish(size)) {
        return false;
    }
    return send_transport(to, {buffer.data(), size});
}

bool enqueue_reliable(std::uint64_t sessionId,
                      std::uint8_t id,
                      std::uint32_t declaredSize,
                      std::span<const std::byte> body,
                      std::size_t bodyBits) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_session_locked(sessionId);
    const bool queued =
        peer != nullptr && wire::enqueue_message(peer->outbound, id, declaredSize, body, bodyBits);
    if (queued) {
        peer->acknowledgementOwed = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return queued;
}

bool enqueue_reliable_to(const state::gameplay::Endpoint& endpoint,
                         std::uint64_t sessionId,
                         std::uint8_t id,
                         std::uint32_t declaredSize,
                         std::span<const std::byte> body,
                         std::size_t bodyBits) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_endpoint_session_locked(endpoint, sessionId);
    const bool queued =
        peer != nullptr && wire::enqueue_message(peer->outbound, id, declaredSize, body, bodyBits);
    if (queued) {
        peer->acknowledgementOwed = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return queued;
}

bool remote_address(std::uint64_t sessionId,
                    std::array<std::byte, state::gameplay::kNetAddrBlobSize>& output) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* peer = find_session_locked(sessionId);
    const bool present = peer != nullptr && peer->remoteAddressPresent;
    if (present) {
        output = peer->remoteAddress;
    }
    ReleaseSRWLockShared(&g_lock);
    return present;
}

bool remote_address_for(
    const state::gameplay::Endpoint& endpoint,
    std::uint64_t sessionId,
    std::array<std::byte, state::gameplay::kNetAddrBlobSize>& output) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* peer = find_endpoint_session_locked(endpoint, sessionId);
    const bool present = peer != nullptr && peer->remoteAddressPresent;
    if (present) {
        output = peer->remoteAddress;
    }
    ReleaseSRWLockShared(&g_lock);
    return present;
}

bool application_ready(std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* const peer = find_session_locked(sessionId);
    const bool ready = peer != nullptr && peer->applicationReady;
    ReleaseSRWLockShared(&g_lock);
    return ready;
}


bool application_ready_for(const state::gameplay::Endpoint& endpoint,
                           std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* const peer =
        find_endpoint_session_locked(endpoint, sessionId);
    const bool ready = peer != nullptr && peer->applicationReady;
    ReleaseSRWLockShared(&g_lock);
    return ready;
}

void bind_view(std::uint64_t sessionId, const state::gameplay::ViewSignature& signature) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_session_locked(sessionId);
    if (peer != nullptr) {
        peer->view = signature;
        peer->postQueueProbePackets = 0;
        peer->postQueueSecondBitPackets = 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void bind_view_for(const state::gameplay::Endpoint& endpoint,
                   std::uint64_t sessionId,
                   const state::gameplay::ViewSignature& signature) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* peer = find_endpoint_session_locked(endpoint, sessionId);
    if (peer != nullptr) {
        peer->view = signature;
        peer->postQueueProbePackets = 0;
        peer->postQueueSecondBitPackets = 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool view_bound(std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* peer = find_session_locked(sessionId);
    const bool bound = peer != nullptr && peer->view.bound;
    ReleaseSRWLockShared(&g_lock);
    return bound;
}

bool view_bound_for(const state::gameplay::Endpoint& endpoint,
                    std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* peer = find_endpoint_session_locked(endpoint, sessionId);
    const bool bound = peer != nullptr && peer->view.bound;
    ReleaseSRWLockShared(&g_lock);
    return bound;
}

bool link_stage(std::uint64_t sessionId, state::gameplay::PeerStage& stage) noexcept {
    stage = state::gameplay::PeerStage::absent;
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::PeerLink* peer = find_session_locked(sessionId);
    const bool present = peer != nullptr;
    if (present) {
        stage = peer->stage;
    }
    ReleaseSRWLockShared(&g_lock);
    return present;
}

void service(std::uint64_t now) noexcept {
    // resolve ownership without nesting the group lock under this one; a late peer gets membership
    // first and only its next service slice appends the retained profile body and deltas
    std::array<PlayerPropertiesReplayTarget, kTransportPeerCapacity> targets{};
    std::size_t targetCount = 0;
    AcquireSRWLockShared(&g_lock);
    for (const state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage == state::gameplay::PeerStage::absent || !peer.applicationReady
            || targetCount >= targets.size()) {
            continue;
        }
        PlayerPropertiesReplayTarget& target = targets[targetCount++];
        target.endpoint = peer.endpoint;
        target.occupied = true;
        for (const std::uint64_t sessionId : peer.sessions) {
            if (sessionId != 0 && target.sessionCount < target.sessions.size()) {
                target.sessions[target.sessionCount++] = sessionId;
            }
        }
    }
    ReleaseSRWLockShared(&g_lock);
    for (std::size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        PlayerPropertiesReplayTarget& target = targets[targetIndex];
        std::size_t authorizedCount = 0;
        const std::size_t candidateCount = target.sessionCount;
        for (std::size_t sessionIndex = 0; sessionIndex < candidateCount; ++sessionIndex) {
            const std::uint64_t sessionId = target.sessions[sessionIndex];
            if (group::ready_for_player_properties_replay(target.endpoint, sessionId)) {
                target.sessions[authorizedCount++] = sessionId;
            }
        }
        for (std::size_t index = authorizedCount; index < target.sessions.size(); ++index) {
            target.sessions[index] = 0;
        }
        target.sessionCount = authorizedCount;
    }

    std::array<state::gameplay::PeerLink, kTransportPeerCapacity> owed{};
    std::size_t count = 0;
    static_assert(sizeof(owed) < 2 * 1024 * 1024, "Keep transport snapshots within the server stack budget");
    std::size_t playerPropertiesReplayed = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage != state::gameplay::PeerStage::absent && peer.applicationReady) {
            for (std::size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
                const PlayerPropertiesReplayTarget& target = targets[targetIndex];
                if (!target.occupied || !same_endpoint(target.endpoint, peer.endpoint)) {
                    continue;
                }
                playerPropertiesReplayed += replay_player_properties_locked(
                    peer,
                    std::span<const std::uint64_t>(target.sessions).first(target.sessionCount),
                    kPlayerPropertiesReplayPerSlice);
                break;
            }
        }
        const bool resendDue = peer.outbound.count != 0 && now - peer.lastSend >= kResendInterval;
        const bool due = peer.acknowledgementOwed || resendDue;
        if (peer.stage == state::gameplay::PeerStage::absent || !due) {
            continue;
        }
        peer.acknowledgementOwed = false;
        peer.lastSend = now;
        if (peer.outbound.count != 0 && !peer.outbound.awaitingAcknowledgement) {
            peer.outbound.sentInPacket =
                static_cast<std::uint16_t>((peer.outboundHead + 1) % kPacketSequenceModulus);
            peer.outbound.sentCount = wire::reliable_send_batch(peer.outbound, false);
            peer.outbound.awaitingAcknowledgement = true;
            peer.outbound.carryingCount = 0;
            peer.outbound.carryingPackets = {};
        }
        peer.outboundHead =
            static_cast<std::uint16_t>((peer.outboundHead + 1) % kPacketSequenceModulus);
        ++g_issuedOrdinals[static_cast<std::size_t>(&peer - g_peers.data())];
        peer.outboundHeadPresent = true;
        wire::record_carrier(peer.outbound, peer.outboundHead);
        peer.lastTick = now;
        owed[count] = peer;
        ++count;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (playerPropertiesReplayed != 0) {
        report(core::log::Level::info,
               "ev=gameplay stage=player_properties result=replayed queued=%zu",
               playerPropertiesReplayed);
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!send_acknowledgement(owed[index])) {
            report(core::log::Level::debug, "ev=gameplay stage=ack result=fail");
        }
    }
}

void drop(std::uint64_t sessionId) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* const peer = find_session_locked(sessionId);
    if (peer != nullptr) {
        for (std::uint64_t& slot : peer->sessions) {
            if (slot == sessionId) {
                slot = 0;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void drop_for(const state::gameplay::Endpoint& endpoint, std::uint64_t sessionId) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::PeerLink* const peer = find_endpoint_session_locked(endpoint, sessionId);
    if (peer != nullptr) {
        for (std::uint64_t& slot : peer->sessions) {
            if (slot == sessionId) {
                slot = 0;
            }
        }
    }
    discard_source_publications_locked(endpoint, sessionId);
    clear_delivery_locked(endpoint, sessionId);
    ReleaseSRWLockExclusive(&g_lock);
}

void drop_endpoint(const state::gameplay::Endpoint& endpoint) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::PeerLink& peer : g_peers) {
        if (peer.stage != state::gameplay::PeerStage::absent
            && same_endpoint(peer.endpoint, endpoint)) {
            peer = {};
            g_ackContexts[static_cast<std::size_t>(&peer - g_peers.data())] = {};
            g_nativeStamps[static_cast<std::size_t>(&peer - g_peers.data())] = {};
            g_issuedOrdinals[static_cast<std::size_t>(&peer - g_peers.data())] = 0;
            static_cast<void>(g_fragments[static_cast<std::size_t>(&peer - g_peers.data())].reset(0, 0, 0));
        }
    }
    discard_source_publications_locked(endpoint);
    clear_delivery_locked(endpoint);
    ReleaseSRWLockExclusive(&g_lock);
}

void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::PeerLink& peer : g_peers) {
        peer = {};
    }
    g_ackContexts = {};
    g_nativeStamps = {};
    g_issuedOrdinals = {};
    for (auto& fragment : g_fragments) static_cast<void>(fragment.reset(0, 0, 0));
    g_playerPropertiesCache = {};
    g_playerPropertiesSequence = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::server::gameplay::peer
