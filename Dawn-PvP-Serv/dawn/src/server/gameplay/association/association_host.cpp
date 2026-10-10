#include "association_host.h"

#include <Windows.h>
#include "../transport_capacity.h"

#include <array>

#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/gameplay/association/control_packet.h"
#include "../../../middleware/gameplay/association/protected_datagram.h"
#include "../../../middleware/gameplay/association/srp_exchange.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_log.h"
#include "../group/group_host.h"
#include "../peer/peer_transport.h"

namespace dawn::server::gameplay::association {

namespace {

namespace wire = middleware::gameplay::association;
namespace srp = middleware::gameplay::association::srp;

constexpr unsigned kMarkerShift = 7;
constexpr std::size_t kMinimumDatagram = 3;

SRWLOCK g_lock{SRWLOCK_INIT};
struct AssociationState {
    std::array<state::gameplay::Association, kTransportPeerCapacity> associations{};
    bool endpointBound{};
    state::gameplay::Endpoint bound{};
};
AssociationState g_state;

[[nodiscard]] bool is_control(std::span<const std::byte> datagram) noexcept {
    return (std::to_integer<unsigned>(datagram[0]) >> kMarkerShift) != 0;
}

[[nodiscard]] bool same_endpoint(const state::gameplay::Endpoint& left,
                                 const state::gameplay::Endpoint& right) noexcept {
    return left.address == right.address && left.port == right.port;
}

void drop_locked(state::gameplay::Association& association) noexcept {
    SecureZeroMemory(association.protection.key.data(), association.protection.key.size());
    SecureZeroMemory(association.protection.outboundBase.data(),
                     association.protection.outboundBase.size());
    SecureZeroMemory(association.protection.inboundBase.data(),
                     association.protection.inboundBase.size());
    association = {};
}

[[nodiscard]] state::gameplay::Association*
find_locked(const state::gameplay::Endpoint& from) noexcept {
    for (state::gameplay::Association& association : g_state.associations) {
        if (association.stage != state::gameplay::AssociationStage::absent
            && same_endpoint(association.endpoint, from)) {
            return &association;
        }
    }
    return nullptr;
}

[[nodiscard]] state::gameplay::Association* allocate_locked() noexcept {
    for (state::gameplay::Association& association : g_state.associations) {
        if (association.stage == state::gameplay::AssociationStage::absent) {
            return &association;
        }
    }
    return nullptr;
}

[[nodiscard]] bool build_response(const wire::ControlPacket& offer,
                                  wire::ControlPacket& response,
                                  state::gameplay::ProtectedContext& context) noexcept {
    srp::Exchange exchange{};
    exchange.verifier = offer.verifier;
    exchange.clientPublic = offer.publicValue;
    if (!srp::derive(exchange)) {
        return false;
    }
    response = {};
    response.opcode = wire::Opcode::keyExchangeResponse;
    response.responseValue = offer.wordB;
    response.hasSignOnBlob = false;
    response.publicValue = exchange.serverPublic;
    if (!middleware::crypto::random::fill(response.networkId)
        || !middleware::crypto::random::fill(response.identityNonce)
        || !middleware::crypto::random::fill(response.verifier)
        || !middleware::crypto::random::fill(response.seed)) {
        return false;
    }
    std::array<std::byte, sizeof(std::uint32_t) * 2> words{};
    if (!middleware::crypto::random::fill(words)) {
        return false;
    }
    context = {};
    for (std::size_t index = 0; index < context.key.size(); ++index) {
        context.key[index] = exchange.derivedKey[index];
    }
    context.outboundBase = response.seed;
    context.inboundBase = offer.seed;
    for (std::size_t index = 0; index < sizeof(std::uint32_t); ++index) {
        constexpr unsigned kByteBits = 8;
        const unsigned shift = static_cast<unsigned>(index) * kByteBits;
        context.outboundWordA |= std::to_integer<std::uint32_t>(words[index]) << shift;
        context.outboundWordB |=
            std::to_integer<std::uint32_t>(words[index + sizeof(std::uint32_t)]) << shift;
    }
    context.installed = true;
    response.wordA = context.outboundWordA;
    response.wordB = context.outboundWordB;
    return true;
}

void accept_offer(const state::gameplay::Endpoint& from,
                  const wire::ControlPacket& offer,
                  std::uint64_t now) noexcept {
    wire::ControlPacket response{};
    state::gameplay::ProtectedContext context{};
    bool answered = false;

    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* association = find_locked(from);
    if (association != nullptr && association->answered && offer.wordB <= association->offerWordB) {
        // a retry keeps its word B, and the native responder doesn't answer it twice
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::debug,
               "ev=gameplay stage=offer result=drop reason=stale wordb=%u",
               offer.wordB);
        return;
    }
    if (association == nullptr) {
        association = allocate_locked();
    }
    if (association == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::warn, "ev=gameplay stage=offer result=fail reason=capacity");
        return;
    }
    ReleaseSRWLockExclusive(&g_lock);

    // the exchange is far slower than any packet path, so it runs outside the lock
    if (!build_response(offer, response, context)) {
        report(core::log::Level::warn, "ev=gameplay stage=offer result=fail reason=exchange");
        return;
    }

    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* target = find_locked(from);
    if (target == nullptr) {
        target = allocate_locked();
    }
    if (target != nullptr && (!target->answered || offer.wordB > target->offerWordB)) {
        target->endpoint = from;
        target->protection = context;
        target->stage = state::gameplay::AssociationStage::established;
        target->offerWordA = offer.wordA;
        target->offerWordB = offer.wordB;
        target->answered = true;
        target->acceptedWordA = 0;
        target->acceptedAny = false;
        target->createdTick = now;
        target->lastTick = now;
        answered = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!answered) {
        return;
    }

    std::array<std::byte, wire::kControlCapacity> datagram{};
    std::size_t size = 0;
    if (!wire::encode(response, from.port, datagram, size)
        || !endpoint::send_to(from, {datagram.data(), size})) {
        report(core::log::Level::warn, "ev=gameplay stage=response result=fail reason=send");
        return;
    }
    report(core::log::Level::info,
           "ev=gameplay stage=response result=ok wordb=%u signon=%u",
           offer.wordB,
           offer.hasSignOnBlob ? 1U : 0U);
}

void accept_protected(const state::gameplay::Endpoint& from,
                      std::span<const std::byte> datagram,
                      std::uint64_t now) noexcept {
    state::gameplay::ProtectedContext context{};
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::Association* association = find_locked(from);
    const bool established =
        association != nullptr
        && association->stage == state::gameplay::AssociationStage::established;
    if (established) {
        context = association->protection;
    }
    ReleaseSRWLockShared(&g_lock);
    if (!established) {
        return;
    }

    std::array<std::byte, wire::kMaximumPayload> payload{};
    std::size_t payloadSize = 0;
    std::uint32_t wordA = 0;
    if (!wire::open(context, datagram, payload, payloadSize, wordA)) {
        report(core::log::Level::debug, "ev=gameplay stage=protected result=drop reason=tag");
        return;
    }

    bool fresh = false;
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* target = find_locked(from);
    if (target != nullptr && target->stage == state::gameplay::AssociationStage::established) {
        fresh = !target->acceptedAny || wordA > target->acceptedWordA;
        if (fresh) {
            target->acceptedWordA = wordA;
            target->acceptedAny = true;
            target->lastTick = now;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!fresh) {
        report(core::log::Level::debug, "ev=gameplay stage=protected result=drop reason=replay");
        return;
    }
    peer::deliver(from, {payload.data(), payloadSize}, now);
}

} // namespace

void route(const state::gameplay::Endpoint& from,
           std::span<const std::byte> datagram,
           std::uint64_t now) noexcept {
    if (datagram.size() < kMinimumDatagram) {
        return;
    }
    if (!is_control(datagram)) {
        accept_protected(from, datagram, now);
        return;
    }
    wire::ControlPacket packet{};
    if (!wire::decode(datagram, packet)) {
        report(core::log::Level::debug, "ev=gameplay stage=control result=drop reason=decode");
        return;
    }
    if (packet.opcode == wire::Opcode::keyExchangeOffer) {
        accept_offer(from, packet, now);
        return;
    }
    report(core::log::Level::debug,
           "ev=gameplay stage=control result=ignore opcode=%u",
           static_cast<unsigned>(packet.opcode));
}

bool send_payload(const state::gameplay::Endpoint& to,
                  std::span<const std::byte> payload) noexcept {
    std::array<std::byte, wire::kProtectedCapacity> datagram{};
    std::size_t size = 0;
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* association = find_locked(to);
    const bool sealed = association != nullptr
                        && association->stage == state::gameplay::AssociationStage::established
                        && wire::seal(association->protection, payload, to.port, datagram, size);
    ReleaseSRWLockExclusive(&g_lock);
    return sealed && endpoint::send_to(to, {datagram.data(), size});
}

void expire(std::uint64_t now) noexcept {
    std::array<state::gameplay::Endpoint, kTransportPeerCapacity> dropped{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::Association& association : g_state.associations) {
        const bool live = association.stage != state::gameplay::AssociationStage::absent;
        if (live && now - association.lastTick > state::gameplay::kAssociationTimeoutMs) {
            dropped[count] = association.endpoint;
            ++count;
            drop_locked(association);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    for (std::size_t index = 0; index < count; ++index) {
        peer::drop_endpoint(dropped[index]);
        // admitted records and host sessions go with the link; nothing else reclaims them if the peer never leaves
        group::release_endpoint(dropped[index]);
        report(core::log::Level::info, "ev=gameplay stage=association result=timeout");
    }
}

void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::Association& association : g_state.associations) {
        drop_locked(association);
    }
    g_state.endpointBound = false;
    g_state.bound = {};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::server::gameplay::association
