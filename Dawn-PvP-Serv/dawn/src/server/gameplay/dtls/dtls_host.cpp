#include "dtls_host.h"

#include <Windows.h>
#include "../transport_capacity.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <type_traits>

#include "../../../middleware/crypto/ecc_p224.h"
#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/gameplay/dtls/association_keys.h"
#include "../../../middleware/gameplay/dtls/dtls_messages.h"
#include "../../../middleware/gameplay/dtls/record.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_log.h"
#include "../peer/peer_transport.h"

namespace dawn::server::gameplay::dtls {

namespace {

namespace wire = middleware::gameplay::dtls;

constexpr std::size_t kAssociationCapacity = kTransportDtlsCapacity;
constexpr std::uint64_t kHandshakeTimeout = 30000;

enum class Stage : std::uint8_t { absent, cookieWait, established };

struct Association {
    state::gameplay::Endpoint endpoint{};
    Stage stage{Stage::absent};
    std::uint16_t requesterTag{};
    std::uint16_t responderTag{};
    // id the peer routes on; every packet must repeat it or the peer never sees it
    wire::SecurityId securityId{};
    std::array<std::byte, wire::kInitAckSize> issued{};
    middleware::gameplay::dtls::RecordContext record{};
    bool authKnown{};
    std::uint32_t sendSequence{1};
    std::uint64_t touched{};
    std::uint64_t opened{};
    std::uint64_t heard{};
};

struct Preference {
    state::gameplay::Endpoint endpoint{};
    wire::SecurityId securityId{};
    std::array<std::byte, middleware::gameplay::dtls::kSecurityKeySize> securityKey{};
    bool occupied{};
    bool keyPresent{};
    bool preferred{};
    bool reported{};
};

std::uint64_t g_openClock{0};

constexpr std::array<std::byte, middleware::gameplay::dtls::kSecurityKeySize> kJoinKey{};

constexpr unsigned kMaxRecordReports = 24;
constexpr std::uint8_t kRecordType = 6;

std::atomic<unsigned> g_recordReported{0};

std::array<Association, kAssociationCapacity> g_associations{};
std::array<Preference, kAssociationCapacity> g_preferences{};

void to_hex(std::span<const std::byte> input, std::span<char> output) noexcept {
    constexpr std::string_view kDigits{"0123456789abcdef"};
    constexpr unsigned kNibbleBits = 4;
    constexpr unsigned kNibbleMask = 0xF;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto value = std::to_integer<unsigned>(input[index]);
        output[index * 2] = kDigits[(value >> kNibbleBits) & kNibbleMask];
        output[(index * 2) + 1] = kDigits[value & kNibbleMask];
    }
    output[input.size() * 2] = '\0';
}

[[nodiscard]] bool same_endpoint(const state::gameplay::Endpoint& left,
                                 const state::gameplay::Endpoint& right) noexcept {
    return left.address == right.address && left.port == right.port;
}

[[nodiscard]] wire::SecurityId security_id(std::uint64_t value) noexcept {
    wire::SecurityId output{};
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
    return output;
}

[[nodiscard]] Preference* find_preference(const state::gameplay::Endpoint& endpoint) noexcept {
    for (Preference& preference : g_preferences) {
        if (preference.occupied && same_endpoint(preference.endpoint, endpoint)) {
            return &preference;
        }
    }
    return nullptr;
}

[[nodiscard]] Association* acquire(const state::gameplay::Endpoint& from,
                                   const wire::SecurityId& securityId,
                                   std::uint64_t now) noexcept {
    Association* free = nullptr;
    Association* oldest = nullptr;
    for (Association& association : g_associations) {
        if (association.stage != Stage::absent && same_endpoint(association.endpoint, from)
            && association.securityId == securityId) {
            // the peer restarts its own handshake on every retry, so its own id reuses the slot
            return &association;
        }
        if (free == nullptr
            && (association.stage == Stage::absent
                || (association.stage == Stage::cookieWait
                    && now - association.touched > kHandshakeTimeout))) {
            free = &association;
        }
        if (association.stage == Stage::established
            && (oldest == nullptr || association.touched < oldest->touched)) {
            oldest = &association;
        }
    }
    return free != nullptr ? free : oldest;
}

[[nodiscard]] Association* find_addressed(const state::gameplay::Endpoint& from,
                                          std::uint16_t tag) noexcept {
    for (Association& association : g_associations) {
        if (association.stage == Stage::established && same_endpoint(association.endpoint, from)
            && association.responderTag == tag) {
            return &association;
        }
    }
    return nullptr;
}

// the peer reads where it writes, so replies go on the association its records last arrived on
[[nodiscard]] Association* find_sending(const state::gameplay::Endpoint& to) noexcept {
    Preference* const preference = find_preference(to);
    Association* chosen = nullptr;
    for (Association& association : g_associations) {
        if (association.stage != Stage::established || !same_endpoint(association.endpoint, to)) {
            continue;
        }
        if (preference != nullptr && preference->preferred
            && association.securityId == preference->securityId) {
            if (!preference->reported) {
                preference->reported = true;
                report(core::log::Level::info,
                       "ev=gameplay stage=dtls_preference result=selected endpoint=0x%08X:%u",
                       to.address,
                       static_cast<unsigned>(to.port));
            }
            return &association;
        }
        // both stamps start at zero, so a fresh association wins only until a record arrives
        if (chosen == nullptr || association.heard > chosen->heard
            || (association.heard == chosen->heard && association.opened > chosen->opened)) {
            chosen = &association;
        }
    }
    return chosen;
}

[[nodiscard]] Association* find_handshake(const state::gameplay::Endpoint& from,
                                          const wire::SecurityId& securityId) noexcept {
    for (Association& association : g_associations) {
        if (association.stage != Stage::absent && same_endpoint(association.endpoint, from)
            && association.securityId == securityId) {
            return &association;
        }
    }
    return nullptr;
}

[[nodiscard]] bool generate_tag(std::uint16_t& output) noexcept {
    constexpr unsigned kByteBits = 8;
    std::array<std::byte, sizeof(std::uint16_t)> bytes{};
    if (!middleware::crypto::random::fill(bytes)) {
        return false;
    }
    const auto value =
        static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[0])
                                   | (std::to_integer<std::uint16_t>(bytes[1]) << kByteBits));
    output = value == 0 ? 1 : value;
    return true;
}

void on_init(const state::gameplay::Endpoint& from,
             std::span<const std::byte> datagram,
             std::uint64_t now) noexcept {
    wire::Init init{};
    if (!wire::read_init(datagram, init)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=dtls result=drop reason=init_decode bytes=%zu",
               datagram.size());
        return;
    }
    Association* association = acquire(from, init.securityId, now);
    std::uint16_t responderTag = 0;
    if (association == nullptr || !generate_tag(responderTag)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=dtls result=drop reason=%s",
               association == nullptr ? "no_slot" : "no_random");
        return;
    }

    wire::InitAck initAck{};
    initAck.requesterTag = init.initTag;
    initAck.responderTag = responderTag;
    initAck.address = from.address;
    initAck.port = from.port;
    initAck.timestamp = static_cast<std::uint32_t>(now);
    initAck.securityId = init.securityId;
    if (!middleware::crypto::random::fill(initAck.cookie)) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=drop reason=no_random");
        return;
    }

    std::array<std::byte, wire::kInitAckSize> encoded{};
    wire::write_init_ack(initAck, encoded);

    *association = {};
    association->endpoint = from;
    association->stage = Stage::cookieWait;
    association->requesterTag = init.initTag;
    association->responderTag = responderTag;
    association->securityId = init.securityId;
    association->issued = encoded;
    association->touched = now;

    const bool sent = endpoint::send_to(from, encoded);
    report(core::log::Level::info,
           "ev=gameplay stage=dtls result=%s step=init_ack peer_tag=0x%04X local_tag=0x%04X",
           sent ? "ok" : "send_failed",
           static_cast<unsigned>(init.initTag),
           static_cast<unsigned>(responderTag));
}

void on_cookie_echo(const state::gameplay::Endpoint& from,
                    std::span<const std::byte> datagram,
                    std::uint64_t now) noexcept {
    wire::CookieEcho echo{};
    if (!wire::read_cookie_echo(datagram, echo)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=dtls result=drop reason=cookie_decode bytes=%zu",
               datagram.size());
        return;
    }
    Association* association = find_handshake(from, echo.securityId);
    if (association == nullptr) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=drop reason=no_association");
        return;
    }
    // the peer returns the init ack whole, so comparing it covers the cookie and its bound fields
    if (echo.echoedInitAck != association->issued) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=drop reason=cookie_mismatch");
        return;
    }

    association->touched = now;

    middleware::crypto::ecc::Agreement agreement{};
    if (!middleware::crypto::ecc::agree(echo.publicKey, agreement)) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=drop reason=key_agreement");
        return;
    }
    std::span<const std::byte> derivationKey{kJoinKey};
    const Preference* const preference = find_preference(from);
    const bool registeredKey = preference != nullptr && preference->keyPresent
                               && preference->securityId == association->securityId;
    if (registeredKey) {
        derivationKey = preference->securityKey;
    }
    if (!middleware::gameplay::dtls::derive(
            agreement.sharedSecret, derivationKey, association->record.keys)) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=drop reason=key_derivation");
        return;
    }
    association->record.sendTag = association->requesterTag;

    wire::CookieAck cookieAck{};
    cookieAck.requesterTag = association->requesterTag;
    cookieAck.securityId = association->securityId;
    cookieAck.publicKey = agreement.publicKey;

    std::array<std::byte, wire::kCookieAckSize> encoded{};
    wire::write_cookie_ack(cookieAck, encoded);
    const bool sent = endpoint::send_to(from, encoded);
    association->stage = sent ? Stage::established : Stage::cookieWait;
    if (sent) {
        ++g_openClock;
        association->opened = g_openClock;
    }
    // TODO: stop logging key material once the digest choice is settled
    std::array<char, (2 * middleware::crypto::ecc::kFieldSize) + 1> secretText{};
    to_hex(agreement.sharedSecret, secretText);
    std::array<char, (2 * middleware::gameplay::dtls::kCypherKeySize) + 1> cypherText{};
    to_hex(association->record.keys.cypher, cypherText);
    report(core::log::Level::info,
           "ev=gameplay stage=dtls result=%s step=cookie_ack peer_tag=0x%04X key=%s "
           "secret=%s cypher=%s",
           sent ? "ok" : "send_failed",
           static_cast<unsigned>(association->requesterTag),
           registeredKey ? "registered" : "join",
           secretText.data(),
           cypherText.data());
}

void on_record(const state::gameplay::Endpoint& from,
               std::span<const std::byte> datagram,
               std::uint64_t now) noexcept {
    std::uint16_t addressed = 0;
    if (!middleware::gameplay::dtls::read_record_tag(datagram, addressed)) {
        return;
    }
    Association* association = find_addressed(from, addressed);
    if (association == nullptr) {
        return;
    }
    // the peer doesn't announce its digest choice; the first record it sends names it
    if (!association->authKnown) {
        if (!middleware::gameplay::dtls::identify_auth(
                association->record.keys, datagram, association->record.authAlgorithm)) {
            if (g_recordReported.fetch_add(1, std::memory_order_relaxed) < kMaxRecordReports) {
                report(core::log::Level::warn,
                       "ev=gameplay stage=dtls result=drop reason=auth_unknown bytes=%zu",
                       datagram.size());
            }
            return;
        }
        association->authKnown = true;
        report(core::log::Level::info,
               "ev=gameplay stage=dtls result=ok step=auth digest=%u",
               static_cast<unsigned>(association->record.authAlgorithm));
    }

    std::array<std::byte, middleware::gameplay::dtls::kRecordCapacity> payload{};
    std::size_t size = 0;
    std::uint32_t sequence = 0;
    if (!middleware::gameplay::dtls::open(association->record, datagram, payload, size, sequence)) {
        if (g_recordReported.fetch_add(1, std::memory_order_relaxed) < kMaxRecordReports) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=dtls result=drop reason=record bytes=%zu",
                   datagram.size());
        }
        return;
    }
    association->touched = now;
    ++g_openClock;
    association->heard = g_openClock;
    if (g_recordReported.fetch_add(1, std::memory_order_relaxed) < kMaxRecordReports) {
        report(core::log::Level::info,
               "ev=gameplay stage=dtls result=ok step=record seq=%u bytes=%zu",
               sequence,
               size);
    }
    peer::deliver(from, {payload.data(), size}, now);
}

} // namespace

bool send_payload(const state::gameplay::Endpoint& to,
                  std::span<const std::byte> payload) noexcept {
    Association* association = find_sending(to);
    if (association == nullptr) {
        return false;
    }
    std::array<std::byte, middleware::gameplay::dtls::kRecordCapacity> datagram{};
    std::size_t size = 0;
    if (!middleware::gameplay::dtls::seal(
            association->record, association->sendSequence, payload, datagram, size)) {
        report(core::log::Level::warn, "ev=gameplay stage=dtls result=fail step=seal");
        return false;
    }
    ++association->sendSequence;
    return endpoint::send_to(to, {datagram.data(), size});
}

void register_security_key(const state::gameplay::Endpoint& peer,
                           std::uint64_t securityId,
                           std::span<const std::byte> securityKey) noexcept {
    if (securityId == 0
        || securityKey.size() != middleware::gameplay::dtls::kSecurityKeySize) {
        report(core::log::Level::warn,
               "ev=gameplay stage=dtls_key result=rejected endpoint=0x%08X:%u "
               "security=0x%016llX bytes=%zu",
               peer.address,
               static_cast<unsigned>(peer.port),
               static_cast<unsigned long long>(securityId),
               securityKey.size());
        return;
    }
    Preference* target = find_preference(peer);
    if (target == nullptr) {
        for (Preference& candidate : g_preferences) {
            if (!candidate.occupied) {
                target = &candidate;
                break;
            }
        }
    }
    if (target == nullptr) {
        target = &g_preferences[0];
    }
    *target = {};
    target->endpoint = peer;
    target->securityId = security_id(securityId);
    std::copy(securityKey.begin(), securityKey.end(), target->securityKey.begin());
    target->occupied = true;
    target->keyPresent = true;

    // a speculative pre-activation handshake used the join key; force the next retry to rederive
    // with the now-known migration key
    for (Association& association : g_associations) {
        if (association.stage != Stage::absent && same_endpoint(association.endpoint, peer)
            && association.securityId == target->securityId) {
            SecureZeroMemory(&association, sizeof(association));
        }
    }
    report(core::log::Level::info,
           "ev=gameplay stage=dtls_key result=registered endpoint=0x%08X:%u "
           "security=0x%016llX",
           peer.address,
           static_cast<unsigned>(peer.port),
           static_cast<unsigned long long>(securityId));
}

void prefer_security_id(const state::gameplay::Endpoint& peer,
                        std::uint64_t securityId) noexcept {
    if (securityId == 0) {
        return;
    }
    Preference* target = find_preference(peer);
    if (target == nullptr) {
        for (Preference& candidate : g_preferences) {
            if (!candidate.occupied) {
                target = &candidate;
                break;
            }
        }
    }
    if (target == nullptr) {
        target = &g_preferences[0];
    }
    const wire::SecurityId requested = security_id(securityId);
    if (!target->occupied || target->securityId != requested) {
        *target = {};
        target->endpoint = peer;
        target->securityId = requested;
        target->occupied = true;
    }
    target->preferred = true;
    target->reported = false;
    report(core::log::Level::info,
           "ev=gameplay stage=dtls_preference result=armed endpoint=0x%08X:%u "
           "security=0x%016llX",
           peer.address,
           static_cast<unsigned>(peer.port),
           static_cast<unsigned long long>(securityId));
}

bool route(const state::gameplay::Endpoint& from,
           std::span<const std::byte> datagram,
           std::uint64_t now) noexcept {
    std::uint8_t type = 0;
    if (!wire::read_type(datagram, type)) {
        return false;
    }
    if (type == static_cast<std::uint8_t>(wire::Type::init)) {
        on_init(from, datagram, now);
        return true;
    }
    if (type == static_cast<std::uint8_t>(wire::Type::cookieEcho)) {
        on_cookie_echo(from, datagram, now);
        return true;
    }
    if (type == kRecordType) {
        on_record(from, datagram, now);
        return true;
    }
    return false;
}

void reset() noexcept {
    // plain assignment can be elided and the table holds derived keys
    static_assert(std::is_trivially_copyable_v<Association>, "the table is erased as raw bytes");
    SecureZeroMemory(g_associations.data(), sizeof(g_associations));
    SecureZeroMemory(g_preferences.data(), sizeof(g_preferences));
    g_openClock = 0;
}

} // namespace dawn::server::gameplay::dtls
