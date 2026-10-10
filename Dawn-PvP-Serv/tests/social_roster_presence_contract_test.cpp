#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#include "middleware/datagen/definitions.h"
#include "middleware/datagen/social_roster/social_roster_encoder.h"
#include "server/bap/social_roster_presence_policy.h"

namespace roster = dawn::middleware::datagen::social_roster;
namespace presence = dawn::server::bap::social_roster_presence;
namespace datagen = dawn::middleware::datagen;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

int main() {
    CHECK(presence::eligible(11, 11, true, true, true, false));
    CHECK(!presence::eligible(0, 0, true, true, true, false));
    CHECK(!presence::eligible(11, 22, true, true, true, false));
    CHECK(!presence::eligible(11, 11, false, true, true, false));
    CHECK(!presence::eligible(11, 11, true, false, true, false));
    CHECK(!presence::eligible(11, 11, true, true, false, false));
    CHECK(!presence::eligible(11, 11, true, true, true, true));
    presence::ActivityChoice choice{};
    CHECK(choice.index == 0);
    choice.consider(100, 8);
    choice.consider(50, 14);
    CHECK(choice.index == 8);
    choice.consider(200, 4095);
    choice.consider(200, -1);
    CHECK(choice.index == 8);
    choice.consider(200, 14);
    CHECK(choice.index == 14);

    presence::ArrivalLatch arrival{};
    CHECK(!arrival.ready(42));
    arrival.note_delivered(42, true, false, false);
    CHECK(!arrival.ready(42));
    arrival.note_delivered(42, true, true, true);
    CHECK(!arrival.ready(42));
    arrival.note_delivered(42, false, true, false);
    CHECK(!arrival.ready(42));
    arrival.note_delivered(0, true, true, false);
    CHECK(!arrival.ready(0));
    arrival.note_delivered(42, true, true, false);
    CHECK(arrival.ready(42) && !arrival.ready(43));
    arrival.note_delivered(42, true, true, true);
    CHECK(arrival.ready(42));
    arrival = {};
    CHECK(!arrival.ready(42));

    std::array<std::byte, datagen::kSocialRosterMemberSize> base{}, zone{}, other{};
    CHECK(roster::encode_member({11, 1060, 1273}, base));
    std::array<std::byte, 80> golden{};
    golden[0] = std::byte{11};
    golden[0x14] = std::byte{0x24}; golden[0x15] = std::byte{0x04};
    golden[0x24] = std::byte{0xF9}; golden[0x25] = std::byte{0x04};
    golden[0x26] = std::byte{0xFF}; golden[0x27] = std::byte{0xFF};
    golden[0x38] = golden[0x39] = golden[0x3A] = std::byte{6};
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (i >= 0x40 && i < 0x44) {
            CHECK(golden[i] == std::byte{0});
            CHECK(base[i] == std::byte{0xFF});
        } else {
            CHECK(base[i] == golden[i]);
        }
    }
    CHECK(roster::encode_member({11, 1060, 1273, 8}, zone));
    CHECK(roster::encode_member({22, 750, 123, 14}, other));
    std::int16_t index = 0;
    std::memcpy(&index, base.data() + 0x0A, 2);
    CHECK(index == 0);
    std::memcpy(&index, zone.data() + 0x0A, 2);
    CHECK(index == 8);
    std::memcpy(&index, other.data() + 0x0A, 2);
    CHECK(index == 14);
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (i != 10 && i != 11) CHECK(base[i] == zone[i]);
    }
    const auto beforeFailure = zone;
    CHECK(!roster::encode_member({11, 1060, 1273, -1}, zone));
    CHECK(zone == beforeFailure);
    CHECK(!roster::encode_member({11, 1060, 1273, -2}, zone));
    CHECK(zone == beforeFailure);
    CHECK(!roster::encode_member({11, 1060, 1273, 4095}, zone));
    CHECK(zone == beforeFailure);
    CHECK(!roster::encode_member({0, 1060, 1273, 8}, zone));
    CHECK(!roster::encode_member({11, 1060, 1273, 8}, std::span(zone).first(79)));
    CHECK(zone == beforeFailure);
    CHECK(roster::encode_member({11, 1060, 1273, 0}, zone));
    CHECK(roster::encode_member({11, 1060, 1273, 4094}, zone));
    std::array<std::byte, datagen::kSocialRosterDirectorySize> directory{};
    CHECK(roster::encode_directory(11, 99, directory));
    std::uint64_t key = 0;
    std::memcpy(&key, directory.data(), 8); CHECK(key == 11);
    std::memcpy(&key, directory.data() + 8, 8); CHECK(key == 11);
    std::memcpy(&key, directory.data() + 0x50, 8); CHECK(key == 99);

    presence::Tracker tracker{};
    CHECK(!tracker.subscribe(0, 0, 0));
    CHECK(tracker.subscribe(11, 0, 0));
    CHECK(tracker.subscribe(22, 14, 0));
    auto& local = tracker.observations[0];
    auto& remote = tracker.observations[1];
    CHECK(!local.changed(999, 8));
    CHECK(!local.changed(1000, 0));
    CHECK(local.changed(2000, 8));
    CHECK(local.publishedActivity == 0);
    CHECK(!local.changed(2001, 8));
    CHECK(local.changed(3000, 8));
    local.publishedActivity = 8;
    CHECK(!local.changed(4000, 8));
    CHECK(local.changed(5000, 0));
    local.publishedActivity = 0;
    CHECK(remote.root == 22 && remote.publishedActivity == 14);
    tracker.unsubscribe(11);
    CHECK(!local.changed(7000, 8));
    CHECK(remote.root == 22);
    CHECK(tracker.subscribe(11, 8, 7000));
    CHECK(!local.changed(8000, 8));
    CHECK(tracker.subscribe(11, 14, 8000));
    CHECK(local.publishedActivity == 14 && remote.root == 22);
    for (std::uint64_t n = 2; n < tracker.observations.size(); ++n) {
        CHECK(tracker.subscribe(100 + n, 0, 0));
    }
    CHECK(!tracker.subscribe(999, 0, 0));
    CHECK(tracker.observations[0].root == 11 && tracker.observations[1].root == 22);
    CHECK(presence::next_poll((std::numeric_limits<std::uint64_t>::max)() - 1)
          == (std::numeric_limits<std::uint64_t>::max)());
    std::puts("PASS: literal baseline preserved except four-byte group sentinel, arrival latch, activity field only, two roots, live-owner eligibility, newest private activity, unsubscribe/re-subscribe, bounded failed-publish retry");
    return 0;
}
