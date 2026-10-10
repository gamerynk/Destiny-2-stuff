#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "middleware/datagen/definitions.h"
#include "middleware/datagen/social_roster/social_roster_encoder.h"

namespace roster = dawn::middleware::datagen::social_roster;
namespace datagen = dawn::middleware::datagen;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

struct Contact {
    std::uint64_t identity{};
    std::int32_t groupKey{};
    std::string foldedName;
    std::uint8_t tieByte{};
};

struct CachedRow {
    std::int32_t groupKey{};
    Contact fallbackContact;
    std::uint64_t displayedIdentity{};
};

// model of the build-86657 singleton path: 1414A2EC0 makes separate rows for keys 0 and -1;
// 1414C0F60 keeps the contact only for -1; cache compare (14149C150) uses groupKey
class SingletonCache {
public:
    std::vector<std::shared_ptr<CachedRow>> publish(const std::vector<Contact>& contacts) {
        std::vector<std::shared_ptr<CachedRow>> provider;
        for (const auto& contact : contacts) {
            const auto existing = std::find_if(rows_.begin(), rows_.end(), [&](const auto& row) {
                return row->groupKey == contact.groupKey
                    && (contact.groupKey != -1
                        || (row->fallbackContact.foldedName == contact.foldedName
                            && row->fallbackContact.tieByte == contact.tieByte));
            });
            std::shared_ptr<CachedRow> row;
            if (existing == rows_.end()) {
                row = std::make_shared<CachedRow>(CachedRow{
                    contact.groupKey, contact.groupKey == -1 ? contact : Contact{}, 0});
                rows_.push_back(row);
            } else {
                row = *existing;
            }
            row->displayedIdentity = contact.identity;
            provider.push_back(row);
        }
        return provider;
    }
private:
    std::vector<std::shared_ptr<CachedRow>> rows_;
};

int main() {
    static_assert(datagen::kSocialRosterMemberSize == 80);
    static_assert(roster::kPresentationGroupKeyOffset == 0x40);
    static_assert(roster::kUnassignedPresentationGroupKey == -1);

    for (const bool reverse : {false, true}) {
        const std::vector<Contact> contacts = reverse
            ? std::vector<Contact>{{22, 0, "bravo", 0}, {11, 0, "alpha", 0}}
            : std::vector<Contact>{{11, 0, "alpha", 0}, {22, 0, "bravo", 0}};
        SingletonCache cache;
        const auto rows = cache.publish(contacts);
        CHECK(rows.size() == 2);
        CHECK(rows[0] == rows[1]);
        CHECK(rows[0]->displayedIdentity == contacts.back().identity);
        CHECK(rows[1]->displayedIdentity == contacts.back().identity);
    }

    std::size_t permutations = 0;
    for (std::size_t count = 2; count <= 6; ++count) {
        std::vector<std::uint64_t> identities(count);
        std::iota(identities.begin(), identities.end(), std::uint64_t{11});
        SingletonCache cache;
        do {
            std::vector<Contact> contacts;
            for (std::size_t i = 0; i < count; ++i) {
                std::array<std::byte, 80> record{};
                const auto activity = static_cast<std::int16_t>(i % 2 == 0 ? 0 : 8);
                CHECK(roster::encode_member({identities[i], 1060, 1273, activity}, record));
                Contact contact{};
                std::memcpy(&contact.identity, record.data(), sizeof contact.identity);
                std::memcpy(&contact.groupKey, record.data() + 0x40, sizeof contact.groupKey);
                CHECK(contact.identity == identities[i]);
                CHECK(contact.groupKey == -1);
                contact.foldedName = "player" + std::to_string(contact.identity);
                for (std::size_t offset = 0x40; offset < 0x44; ++offset) {
                    CHECK(record[offset] == std::byte{0xFF});
                }
                contacts.push_back(contact);
            }
            for (int refresh = 0; refresh < 2; ++refresh) {
                const auto rows = cache.publish(contacts);
                CHECK(rows.size() == count);
                for (std::size_t i = 0; i < count; ++i) {
                    CHECK(rows[i]->displayedIdentity == identities[i]);
                    CHECK(rows[i]->fallbackContact.identity == identities[i]);
                    for (std::size_t j = 0; j < i; ++j) CHECK(rows[i] != rows[j]);
                }
            }
            const auto solo = cache.publish({contacts.front()});
            CHECK(solo.size() == 1 && solo[0]->displayedIdentity == identities.front());
            const auto rejoined = cache.publish(contacts);
            for (std::size_t i = 0; i < count; ++i) {
                CHECK(rejoined[i]->displayedIdentity == identities[i]);
                for (std::size_t j = 0; j < i; ++j) CHECK(rejoined[i] != rejoined[j]);
            }
            ++permutations;
        } while (std::next_permutation(identities.begin(), identities.end()));
    }
    CHECK(permutations == 872);
    SingletonCache sameNameCache;
    const auto sameNameRows = sameNameCache.publish({{11, -1, "same", 0}, {22, -1, "same", 0}});
    CHECK(sameNameRows[0] == sameNameRows[1]);
    SingletonCache tieByteCache;
    const auto tieByteRows = tieByteCache.publish({{11, -1, "same", 1}, {22, -1, "same", 2}});
    CHECK(tieByteRows[0] != tieByteRows[1]);
    std::printf("PASS: production member +0x40 = -1; native singleton-cache model reproduces zero-key last-joiner alias and preserves distinct-name players across %zu 2..6-player permutations, refresh and rejoin; native same-name/tie-byte limit retained\n", permutations);
    return 0;
}
