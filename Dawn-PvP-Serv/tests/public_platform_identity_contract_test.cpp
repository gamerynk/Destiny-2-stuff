#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

#include "server/gameplay/group/public_player_profile.h"
#include "middleware/bap/account_translation/account_translation_response.h"
#include "middleware/bap/account_translation/request_observation.h"
#include "middleware/encoding/bit_raw.h"
#include "middleware/gameplay/group/session_state.h"

namespace wire = dawn::middleware::gameplay::group;
namespace bits = dawn::middleware::encoding::bits;
namespace trans = dawn::middleware::bap::account_translation;
namespace policy = dawn::server::gameplay::group::public_player_profile;
namespace enc = dawn::middleware::encoding;

static bool eq(bits::Reader& r, std::uint8_t width, std::uint64_t expected) {
    std::uint64_t value = 0;
    return r.read(width, value) && value == expected;
}

static bool wire_contract(const wire::MembershipPlayer& player) {
    std::array<wire::MembershipMember, 2> members{};
    members[0].machineId = 0x11223344;
    members[1].machineId = 0x55667788;
    members[1].ownsPlayerSlot = true;
    members[1].ownedPlayerCountPresent = false;
    members[1].playerSlot = 0;
    wire::MembershipUpdate update{};
    update.hostMachineId = members[0].machineId;
    update.revision = 3;
    update.members = members;
    update.players = std::span(&player, 1);
    update.nextPlayerAdditionNumber = player.addSequence + 1;
    wire::SessionState state{};
    wire::build_session_state(update, state);
    constexpr std::size_t row = 0x3B58;
    for (std::size_t i = 0; i < player.identity.size(); ++i) {
        if (state[row + 0xA0 + i] != player.identity[i]) return false;
    }
    if (player.displayNamePresent) {
        // decode the canonical name the way native BE7850 does
        for (std::size_t i = 0; i < player.displayName.size(); ++i) {
            std::uint16_t stored = 0;
            std::memcpy(&stored, state.data() + row + 0x20 + i * 2, sizeof(stored));
            const auto mask = i == 0 ? 0U : std::rotl(0xC245B0C4U, static_cast<int>(i % 31));
            const auto plain = static_cast<std::uint16_t>(
                (stored ^ static_cast<std::uint16_t>(mask)) * 0xBBAFU);
            if (plain != player.displayName[i]) return false;
            if (plain == 0) break;
        }
    }
    const auto& profile = player.profileSupplement;
    for (std::size_t i = 0; i < 2; ++i) {
        std::uint64_t value{};
        std::memcpy(&value, state.data() + row + 0xE0 + i * 8, sizeof value);
        if (value != (profile.field5Present ? profile.field5[i] : 0)) return false;
    }
    std::array<std::byte, 2048> output{};
    bits::Writer w(output);
    if (!wire::write_membership_update(w, update)) return false;
    const auto meaningful = w.bit_count();
    std::size_t size = 0;
    if (!w.finish(size)) return false;
    bits::Reader r(std::span<const std::byte>(output.data(), size), meaningful);
    std::uint64_t protoBytes = 0;
    if (!r.skip(64) || !r.read(13, protoBytes)
        || !r.skip(static_cast<std::size_t>(protoBytes) * 8)
        || !r.skip(1 + 32 + 32) || !eq(r, 6, 2) || !eq(r, 6, 1)) return false;
    for (const auto& member : members) {
        (void)member;
        if (!r.skip(6 + 1 + 4) || !eq(r, 1, 0) || !eq(r, 3, 0)) return false;
    }
    std::uint64_t id = 0;
    if (!eq(r, 5, player.slot) || !eq(r, 1, 1) || !eq(r, 1, 1)
        || !bits::read_raw_u64(r, id) || id != player.playerId
        || !eq(r, 6, player.memberIndex) || !eq(r, 1, 0)
        || !eq(r, 20, player.addSequence) || !eq(r, 1, 0)
        || !eq(r, 1, 1) || !eq(r, 32, player.emblemDefinitionIndex) || !eq(r, 3, 1)
        || !eq(r, 1, player.displayNamePresent ? 1 : 0)) return false;
    if (player.displayNamePresent) {
        for (const auto unit : player.displayName) {
            if (!eq(r, 16, unit)) return false;
            if (unit == 0) break;
        }
    }
    if (!eq(r, 1, 1)) return false;
    std::array<std::byte, 36> identity{};
    if (!bits::read_raw(r, identity) || identity != player.identity) return false;
    if (profile.field5Present) {
        if (!eq(r, 1, 1) || !eq(r, 8, 0xBD)
            || !eq(r, 1, 1) || !eq(r, 6, 0)
            || !eq(r, 1, 1) || !eq(r, 6, 0)
            || !eq(r, 1, 1) || !eq(r, 16, 0) || !eq(r, 16, 0)
            || !eq(r, 1, 1) || !eq(r, 64, profile.field5[0])
            || !eq(r, 64, profile.field5[1]) || !eq(r, 1, 1)
            || !eq(r, 32, std::bit_cast<std::uint32_t>(static_cast<float>(player.light)))) return false;
        for (unsigned i = 0; i < 4; ++i) if (!eq(r, 16, 32767)) return false;
        if (!eq(r, 8, 0)) return false;
    } else if (!eq(r, 6, 0)) return false;
    if (!eq(r, 1, 1) || !eq(r, 32, player.light) || !eq(r, 4, 0)) return false;
    for (unsigned i = 0; i < 3; ++i) if (!eq(r, 32, 0)) return false;
    if (!eq(r, 5, profile.tailFive) || !eq(r, 2, profile.tailTwo) || !eq(r, 1, 0)
        || !r.skip(1 + wire::kPlayerAdditionNumberWidth + 3)
        || !eq(r, 32, wire::session_state_hash(update)) || r.remaining_bits() != 0) return false;
    return true;
}

static bool translation_observation_contract() {
    std::array<std::byte, 12> req{};
    enc::write_u16_be(std::span(req).first<2>(), 1);
    req[2] = std::byte{12}; req[3] = std::byte{255};
    enc::write_u64_be(std::span(req).subspan<4, 8>(), 0x0110000100000002ULL);
    std::uint64_t platform = 0;
    if (!trans::request_platform_id(req, platform)
        || std::strcmp(trans::observe_request(req).reason, "valid_scalar") != 0) return false;
    std::array<std::byte, 20> response{};
    std::size_t written = 0;
    if (!trans::encode_response(req, 0x12345678, response, written) || written != 20) return false;
    if (enc::read_u64_be(std::span(response).subspan<4, 8>()) != platform
        || enc::read_u64_be(std::span(response).subspan<12, 8>()) != 0x12345678) return false;
    for (std::size_t n = 0; n < 12; ++n) {
        if (trans::request_platform_id(std::span(req).first(n), platform)) return false;
    }
    req[2] = std::byte{255};
    if (trans::request_platform_id(req, platform)
        || std::strcmp(trans::observe_request(req).reason, "unsupported_type") != 0
        || trans::observe_request(req).firstIdentity == 0) return false;
    req[2] = std::byte{12};
    enc::write_u64_be(std::span(req).subspan<4, 8>(), 0);
    if (trans::request_platform_id(req, platform)
        || std::strcmp(trans::observe_request(req).reason, "zero_identity") != 0) return false;
    enc::write_u16_be(std::span(req).first<2>(), 2);
    return std::strcmp(trans::observe_request(req).reason, "truncated_list") == 0;
}

int main() {
    for (std::uint64_t n = 1; n <= 12; ++n) {
        const auto platform = 0x0110000100001000ULL + n;
        wire::MembershipPlayer p{};
        p.playerId = 0x88990000 + n;
        p.memberIndex = 1;
        p.addSequence = 2;
        std::array<std::byte, 36> identity{};
        if (!policy::populate(p, platform, false, identity) || !wire_contract(p)) {
            std::fputs("FAIL initial authenticated platform-only profile\n", stderr); return 1;
        }
        identity = p.identity;
        identity[35] = std::byte{0xA5};
        if (policy::populate(p, platform + 1, true, identity)
            || policy::populate(p, 0, false, identity)) return 1;
        p.profileSupplement = wire::make_account_membership_profile_supplement(99, 100, 1);
        if (!policy::populate(p, platform, true, identity) || p.identity != identity
            || p.profileSupplement.field5Present || !wire_contract(p)) {
            std::fputs("FAIL native identity-only wire/state contract\n", stderr); return 1;
        }
        const auto unnamed = p;
        std::array<char, 32> name{};
        std::snprintf(name.data(), name.size(), "RosterUser%llu", static_cast<unsigned long long>(n));
        if (!policy::populate_named(p, platform, true, identity, name.data())
            || !p.displayNamePresent || p.identity != unnamed.identity
            || p.profileSupplement != unnamed.profileSupplement || !wire_contract(p)) return 1;
        const auto named = p;
        const std::array<std::string_view, 4> invalid{
            "", std::string_view("A\0B", 3), std::string_view("\xFF", 1),
            "1234567890123456789012345678901234567890123456789012345678901234"};
        for (const auto bad : invalid) {
            if (policy::populate_named(p, platform, true, identity, bad)
                || p.displayName != named.displayName || p.identity != named.identity
                || p.profileSupplement != named.profileSupplement) return 1;
        }
        if (policy::populate_named(p, platform + 1, true, identity, "OtherUser")) return 1;
        std::array<char, 63> longest{};
        longest.fill('Z');
        if (!policy::populate_named(p, platform, false, identity,
                                   std::string_view(longest.data(), longest.size()))
            || !wire_contract(p)) return 1;

        wire::MembershipPlayer presentation{};
        presentation.profilePresent = true;
        presentation.emblemDefinitionIndex = static_cast<std::uint32_t>(100 + n);
        presentation.light = static_cast<std::int32_t>(1000 + n * 10);
        presentation.profileSupplement = wire::make_account_membership_profile_supplement(
            0x9EAA000100000100ULL + n * 0x100000,
            0x9EAA000100000102ULL + n * 0x100000,
            std::bit_cast<std::uint32_t>(static_cast<float>(presentation.light)));
        p.profileSupplement = wire::make_account_membership_profile_supplement(99, 100, 1);
        if (!policy::populate_presented(p, platform, true, identity, name.data(), presentation)
            || p.identity != identity || !wire_contract(p)) return 1;
        const auto presented = p;
        for (unsigned missing = 0; missing < 4; ++missing) {
            auto incomplete = presentation;
            if (missing == 0) incomplete.profilePresent = false;
            if (missing == 1) incomplete.profileSupplement.field5[0] = 0;
            if (missing == 2) incomplete.profileSupplement.field5[1] = 0;
            if (missing == 3) incomplete.profileSupplement.field6Present = false;
            if (policy::populate_presented(p, platform, true, identity, name.data(), incomplete)
                || p.profileSupplement != presented.profileSupplement
                || p.emblemDefinitionIndex != presented.emblemDefinitionIndex
                || p.light != presented.light) return 1;
        }
        if (policy::populate_presented(p, platform + 1, true, identity, name.data(), presentation)) return 1;
        presentation.profileSupplement.field5[1] += 1;
        presentation.emblemDefinitionIndex += 400;
        presentation.light += 30;
        presentation.profileSupplement.field6FloatBits =
            std::bit_cast<std::uint32_t>(static_cast<float>(presentation.light));
        if (!policy::populate_presented(p, platform, true, identity, name.data(), presentation)
            || p.profileSupplement != presentation.profileSupplement || !wire_contract(p)) return 1;
    }
    if (!translation_observation_contract()) return 1;
    std::puts("PASS: 12 authenticated public profiles, selected-character/emblem/Power changes, independent ID30 association decode/hash, atomic incomplete/spoof rejection, typed svc23 diagnostics");
}
