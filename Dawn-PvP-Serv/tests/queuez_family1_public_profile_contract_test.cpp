#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "middleware/datagen/character_record/layout.h"
#include "middleware/datagen/definitions.h"
#include "middleware/datagen/family1/family1_encoder.h"
#include "middleware/datagen/family4/inventory/layout.h"

namespace {

template <typename Value>
Value read(const std::byte* source) noexcept {
    Value value{};
    std::memcpy(&value, source, sizeof value);
    return value;
}

bool require(bool condition, const char* description) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

} // namespace

int main() {
    namespace character_record = dawn::middleware::datagen::character_record;
    namespace datagen = dawn::middleware::datagen;
    namespace family1 = datagen::family1;
    namespace family4 = datagen::family4;

    constexpr std::uint64_t kAccountSoid = 0x9EAA000100100100ULL;
    constexpr std::uint64_t kCharacterSoid = 0x9EAA000100100101ULL;
    constexpr std::uint64_t kFirstInstance = 0x4100000000000001ULL;
    constexpr std::uint64_t kSecondInstance = 0x4100000000000002ULL;

    std::array<family4::progression::layout::Entry, family1::kProgressionCapacity>
        accountProgressions{};
    std::array<family4::progression::layout::Entry, family1::kProgressionCapacity>
        characterProgressions{};
    accountProgressions[0].values[0] = 111;
    accountProgressions[0].definitionIndex = 12;
    characterProgressions[0].values[0] = 222;
    characterProgressions[0].definitionIndex = 34;

    std::array<std::byte, family1::kRootSize> root{};
    bool passed = family1::encode_root(
        kAccountSoid, kAccountSoid, accountProgressions, root);
    passed &= require(read<std::uint64_t>(root.data()) == kAccountSoid,
                      "Family-1 root account key");
    passed &= require(read<std::uint64_t>(root.data() + 8) == kAccountSoid,
                      "Family-1 root model link aliases the sole account-keyed resident");
    passed &= require(read<std::int32_t>(root.data() + 0x10) == 111,
                      "Family-1 root progression bank begins at +0x10");

    std::array<std::byte, character_record::layout::kIdentitySize> identity{};
    std::memcpy(identity.data(), &kCharacterSoid, sizeof kCharacterSoid);
    identity[8] = std::byte{1};
    identity[9] = std::byte{0};
    identity[10] = std::byte{2};
    identity[44] = std::byte{1};
    std::array<std::byte, character_record::layout::kAppearanceSize> appearance{};
    std::array<std::byte, character_record::layout::kSummarySize> summary{};
    appearance.front() = std::byte{0x5A};
    appearance.back() = std::byte{0xA5};
    summary.front() = std::byte{0x3C};
    summary.back() = std::byte{0xC3};

    family4::loadout::ResolvedLoadout loadout{};
    loadout.itemCount = 3;
    loadout.items[0].equipped = true;
    loadout.items[0].equipmentSlot = 2;
    loadout.items[0].quantity = 1;
    loadout.items[0].mutationSerial = 10;
    loadout.items[0].flags = 0x11223344U;
    loadout.items[0].instance.instanceSoid = kFirstInstance;
    loadout.items[0].instance.baseDefinitionIndex = 101;
    loadout.items[1].equipped = false;
    loadout.items[1].instance.instanceSoid = 0x41000000000000FFULL;
    loadout.items[1].instance.baseDefinitionIndex = 999;
    loadout.items[2].equipped = true;
    loadout.items[2].equipmentSlot = 7;
    loadout.items[2].quantity = 2;
    loadout.items[2].mutationSerial = 20;
    loadout.items[2].flags = 0x55667788U;
    loadout.items[2].instance.instanceSoid = kSecondInstance;
    loadout.items[2].instance.baseDefinitionIndex = 202;

    std::array<std::byte, family1::kBodySize> body{};
    passed &= require(family1::encode_body(kAccountSoid,
                                           identity,
                                           characterProgressions,
                                           loadout,
                                           appearance,
                                           summary,
                                           body),
                      "Family-1 body encodes");
    passed &= require(read<std::uint64_t>(body.data()) == kAccountSoid,
                      "Family-1 body resident key matches the account-keyed QueueZ header");
    passed &= require(body[8] == std::byte{1} && body[9] == std::byte{0}
                          && body[10] == std::byte{2},
                      "Family-1 body preserves character traits beginning at +8");
    passed &= require(std::memcmp(body.data() + sizeof(std::uint64_t),
                                  identity.data() + sizeof(std::uint64_t),
                                  identity.size() - sizeof(std::uint64_t))
                          == 0,
                      "Family-1 body preserves the full character header after the resident key");
    passed &= require(read<std::int32_t>(body.data() + 0x30) == 222,
                      "Family-1 fixed progression array begins at +0x30 without a count");
    passed &= require(read<std::uint32_t>(body.data() + 0x820) == 2U,
                      "Family-1 compact equipped count at +0x820");
    passed &= require(read<std::uint32_t>(body.data() + 0x824) == 0U,
                      "Family-1 inventory wrapper padding at +0x824");

    const auto* firstRow = body.data() + 0x828;
    const auto* secondRow = firstRow + family4::inventory::layout::kEntrySize;
    const auto* thirdRow = secondRow + family4::inventory::layout::kEntrySize;
    passed &= require(read<std::uint16_t>(firstRow) == 101
                          && read<std::uint64_t>(firstRow + 8) == kFirstInstance,
                      "Family-1 first compact inventory row");
    passed &= require(read<std::uint16_t>(secondRow) == 202
                          && read<std::uint64_t>(secondRow + 8) == kSecondInstance,
                      "Family-1 second compact inventory row skips unequipped gear");
    passed &= require(read<std::uint16_t>(thirdRow) == 0xFFFFU,
                      "Family-1 unused inventory rows retain the empty sentinel");
    passed &= require(read<std::uint64_t>(body.data() + 0xAA8 + 2 * sizeof(std::uint64_t))
                              == kFirstInstance
                          && read<std::uint64_t>(body.data()
                                                 + 0xAA8
                                                 + 7 * sizeof(std::uint64_t))
                                 == kSecondInstance,
                      "Family-1 equipment table is indexed by native slot");

    passed &= require(read<std::uint32_t>(body.data() + 0xB48) == 0U,
                      "Family-1 optional row count is zero");
    bool auxiliaryRowsEmpty = true;
    for (std::size_t index = 0; index < 20; ++index) {
        const std::byte* row = body.data() + 0xB4C + index * 0x0C;
        auxiliaryRowsEmpty &= read<std::uint16_t>(row) == 0xFFFFU;
        auxiliaryRowsEmpty &= read<std::uint32_t>(row + 8) == 0x811C9DC5U;
    }
    passed &= require(auxiliaryRowsEmpty,
                      "Family-1 optional rows carry native empty sentinels");
    passed &= require(read<std::uint16_t>(body.data() + 0xC48) == 0xFFFFU,
                      "Family-1 first optional presentation block is empty");
    passed &= require(read<std::uint32_t>(body.data() + 0xC60) == 0x811C9DC5U
                          && read<std::uint16_t>(body.data() + 0xC64) == 0xFFFFU
                          && read<std::uint16_t>(body.data() + 0xC66) == 0xFFFFU
                          && read<std::uint16_t>(body.data() + 0xC68) == 0xFFFFU
                          && read<std::uint16_t>(body.data() + 0xC6C) == 0xFFFFU,
                      "Family-1 second optional presentation block is empty");
    passed &= require(body[0xC80] == std::byte{0x5A}
                          && body[0x1B27] == std::byte{0xA5},
                      "Family-1 appearance occupies +0xC80 through +0x1B27");
    passed &= require(body[0x1B28] == std::byte{0x3C}
                          && body[0x1B47] == std::byte{0xC3},
                      "Family-1 summary occupies the exact tail");

    passed &= require(read<std::uint64_t>(root.data() + 8)
                          == read<std::uint64_t>(body.data()),
                      "Family-1 singleton graph closes root model link onto slot-one body");

    std::uint32_t objectId = 0;
    passed &= require(datagen::object_id(datagen::kPublicProfileFamily,
                                         datagen::kPublicProfileRootSlot,
                                         objectId)
                          && objectId == 0xC47D63DBU,
                      "Family-1 slot-zero PC object id");
    passed &= require(datagen::object_id(datagen::kPublicProfileFamily,
                                         datagen::kPublicCharacterSlot,
                                         objectId)
                          && objectId == 0x1A10412DU,
                      "Family-1 slot-one PC object id");
    passed &= require(datagen::object_id(datagen::kPublicProfileFamily,
                                         datagen::kPublicAuxiliarySlot,
                                         objectId)
                          && objectId == 0x25D87B54U,
                      "Family-1 slot-two PC object id is known but not published");
    passed &= require(datagen::object_id(datagen::kPublicProfileFamily,
                                         datagen::kPublicItemInstanceSlot,
                                         objectId)
                          && objectId == 0x6CFBA3ABU,
                      "Family-1 slot-three PC item id");

    family4::loadout::ResolvedLoadout duplicate = loadout;
    duplicate.items[2].equipmentSlot = duplicate.items[0].equipmentSlot;
    passed &= require(!family1::encode_body(kAccountSoid,
                                            identity,
                                            characterProgressions,
                                            duplicate,
                                            appearance,
                                            summary,
                                            body),
                      "Family-1 duplicate equipment slots are rejected");
    passed &= require(!family1::encode_body(0,
                                            identity,
                                            characterProgressions,
                                            loadout,
                                            appearance,
                                            summary,
                                            body),
                      "Family-1 zero resident SOID is rejected");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: QueueZ Family-1 PC 1.59 public-profile wire contract");
    return 0;
}
