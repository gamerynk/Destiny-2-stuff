#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <vector>
#include "client/hooks/bootflow/special_ammo_handoff.h"

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace pickup = dawn::client::hooks::bootflow::special_ammo_handoff;
template<class T> void put(void* p, std::size_t at, T v) {
    std::memcpy(static_cast<std::byte*>(p) + at, &v, sizeof v);
}
bool check(bool v, const char* name) {
    if (!v) std::fprintf(stderr, "FAIL: %s\n", name);
    return v;
}
struct Fixture {
    static constexpr std::uint32_t legacy = 0x08FAA00D, index = legacy & 0x1FFFU;
    std::byte* image = static_cast<std::byte*>(VirtualAlloc(nullptr, 0x3100000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::vector<std::byte> units = std::vector<std::byte>(4096 * 0xE0);
    std::array<std::byte, 480 * 2> players{};
    std::array<std::byte, 0x388> brick{};
    Fixture() {
        if (!image) return;
        put(image, 0x1F93428, units.data()); put(image, 0x1F93430, std::uint32_t{0xE0});
        put(image, 0x1F93434, std::uint32_t{4096});
        put(units.data(), index * 0xE0 + 0xC, legacy);
        put(units.data(), index * 0xE0 + 0x4C, std::uint32_t{0x7AF9E8BC});
        put(image, 0x1F90E18, players.data()); put(image, 0x1F90E20, std::uint32_t{480});
        put(image, 0x1F90E24, std::uint32_t{2});
        put(players.data(), 0x40, std::uint32_t{3}); put(players.data(), 0x54, std::uint32_t{42});
        put(players.data(), 480 + 0x40, std::uint32_t{2});
        put(players.data(), 480 + 0x54, std::uint32_t{43});
        put(brick.data(), 0, std::uint32_t{0x80C0011D});
        put(brick.data(), 4, std::uint32_t{0x80804494});
        put(brick.data(), 0x2C, legacy);
        put(brick.data(), 0x2D0, std::uint8_t{2});
        put(brick.data(), 0x2D8, 0.024193547666072845F);
        put(brick.data(), 0x2DC, std::uint8_t{1}); put(brick.data(), 0x2DD, std::uint8_t{2});
    }
    ~Fixture() { if (image) VirtualFree(image, 0, MEM_RELEASE); }
    void authority(bool v) { put(image, 0x26BE0E0, v ? 1U << index : 0U); }
    bool prepared(std::uint32_t player = 0) {
        std::uint32_t token{};
        return pickup::prepare(brick.data(), player, reinterpret_cast<std::uintptr_t>(image), token)
            && token == legacy;
    }
};
unsigned eligibleCalls{}, handoffCalls{}, collectCalls{}, grants{};
std::uint32_t requested{}, seenPlayer{};
void* seenComponent{};
DWORD seenError{};
bool eligibleResult = true, grantAllowed = false;
bool __fastcall eligible(void*, std::uint32_t) noexcept {
    ++eligibleCalls; SetLastError(777); return eligibleResult;
}
void __fastcall handoff(std::uint32_t legacy) noexcept {
    ++handoffCalls; requested = legacy; SetLastError(888);
}
void __fastcall collect(void* component, std::uint32_t player) noexcept {
    ++collectCalls; seenComponent = component; seenPlayer = player; seenError = GetLastError();
    if (grantAllowed) { ++grants; grantAllowed = false; }
    SetLastError(321);
}
void reset() {
    pickup::retries = {}; pickup::requests = 0; pickup::lastReport = 0;
    pickup::leases = {}; pickup::qualified = 0; pickup::deferred = 0;
    eligibleCalls = handoffCalls = collectCalls = grants = 0;
    eligibleResult = true; grantAllowed = false;
}
bool native_contract(Fixture& f, const char* text, const char* rdata, const char* oldBase) {
    auto load = [](const char* path, void* destination, std::size_t size) {
        std::ifstream input(path, std::ios::binary);
        return input.read(static_cast<char*>(destination), static_cast<std::streamsize>(size)).good();
    };
    if (!load(text, f.image + 0x1000, 28898816)
        || !load(rdata, f.image + 0x1B91000, 3667968)) return check(false, "native sections loaded");
    const auto base = static_cast<std::uintptr_t>(std::strtoull(oldBase, nullptr, 0));
    std::uintptr_t callback{};
    std::memcpy(&callback, f.image + 0x1CF64F0, sizeof callback);
    if (!check(callback == base + 0xE94C30, "captured collect registry identifies native grant")) return false;
    put(f.image, 0x1CF64F0, reinterpret_cast<std::uintptr_t>(f.image) + 0xE94C30);
    std::memcpy(&callback, f.image + 0x1CF64E0, sizeof callback);
    if (!check(callback == base + 0xE8A870, "captured registry identifies ammo query")) return false;
    put(f.image, 0x1CF64E0, reinterpret_cast<std::uintptr_t>(f.image) + 0xE8A870);
    bool ok = check(pickup::supported(f.image), "all production guards match captured native code");
    f.image[0xE94C81] ^= std::byte{1};
    ok &= check(!pickup::supported(f.image), "changed authority branch disables hook");
    f.image[0xE94C81] ^= std::byte{1};
    DWORD oldProtection{};
    if (!VirtualProtect(f.image + 0xE94000, 0x1000, PAGE_EXECUTE_READ, &oldProtection)) return false;
    FlushInstructionCache(GetCurrentProcess(), f.image + 0xE94000, 0x1000);
    const auto native = reinterpret_cast<pickup::Collect>(f.image + 0xE94C30);
    const auto before = f.brick;
    reset(); f.authority(false);
    native(f.brick.data(), 0);
    ok &= check(handoffCalls == 0 && f.brick == before, "original blocked grant silently returns");
    pickup::original = native; pickup::enabled = true;
    pickup::collect(f.brick.data(), 0);
    std::uint32_t bits{}; std::memcpy(&bits, f.image + 0x26BE0E0, sizeof bits);
    ok &= check(handoffCalls == 1 && requested == Fixture::legacy && bits == 0 && f.brick == before,
        "fallback requests native handoff; actual grant still enforces ownership");
    pickup::original = &collect;
    return ok;
}

constexpr std::uint32_t simulationHandle = 0x2AFBA017, networkToken = 0x40011;
bool geometryPass = true, queryArgsOk{}, transferArgsOk{};
unsigned queryCalls{}, ammoQueryCalls{}, transferCalls{};
std::uint32_t __fastcall simulation(std::uint32_t legacy) noexcept {
    return legacy == Fixture::legacy ? simulationHandle : UINT32_MAX;
}
void __fastcall ammo_query(void*, std::uint32_t, void*, void* result) noexcept {
    ++ammoQueryCalls; put(result, 1, std::uint8_t(eligibleResult ? 2 : 0));
    SetLastError(456);
}
void __fastcall query(void* component, std::uint32_t player, std::uint8_t flags, void* profile,
    std::uintptr_t movement, const void* position, const void* origin, const void* direction,
    bool update, void* result) noexcept {
    ++queryCalls;
    queryArgsOk = flags == 0 && profile == reinterpret_cast<void*>(0x111)
        && movement == 0x222 && position == reinterpret_cast<void*>(0x333)
        && origin == reinterpret_cast<void*>(0x444) && direction == reinterpret_cast<void*>(0x555)
        && update && GetLastError() == 123;
    put(result, 0xC, std::uint8_t{1});
    pickup::ammo_query(component, player, profile, result);
    // native order: E8A870 accepts inventory, then F32CD0 can reject the same result for geometry
    if (!geometryPass) put(result, 1, std::uint8_t{0});
    SetLastError(654);
}
bool __fastcall transfer(void* manager, std::uint32_t token, int peer) noexcept {
    ++transferCalls;
    transferArgsOk = manager && token == networkToken && peer == 0 && GetLastError() == 123;
    SetLastError(987); return true;
}
bool lease_contract(Fixture& f) {
    reset(); f.authority(false);
    pickup::nativeSimulation = &simulation;
    pickup::originalAmmoQuery = &ammo_query;
    pickup::originalQuery = &query;
    pickup::originalTransfer = &transfer;
    std::vector<std::byte> manager(0x11B90);
    constexpr auto slot = 18U;
    auto* row = f.image + 0x30B0440 + slot * 0x70;
    put(manager.data(), 0xC520, 1U << (networkToken & 31));
    put(manager.data(), 0x114 + (networkToken & 0x1FFFU) * 6, std::int16_t(slot));
    put(row, 0, std::uint8_t{0}); put(row, 1, std::uint8_t{0xFF});
    put(row, 4, simulationHandle); put(row, 8, networkToken);
    std::array<std::byte,0x30> result{};
    auto run = [&](std::uint32_t player = 0, std::uint8_t flags = 0, bool update = true) {
        SetLastError(123);
        pickup::query(f.brick.data(), player, flags, reinterpret_cast<void*>(0x111), 0x222,
            reinterpret_cast<void*>(0x333), reinterpret_cast<void*>(0x444),
            reinterpret_cast<void*>(0x555), update, result.data());
    };
    geometryPass = false; run();
    bool ok = check(queryArgsOk && queryCalls == 1 && ammoQueryCalls == 1 && GetLastError() == 654,
        "outer query forwards all ten arguments once and preserves native error");
    ok &= check(handoffCalls == 0 && !pickup::pending(manager.data(), networkToken, GetTickCount64()),
        "inventory eligible but outside pickup geometry cannot claim or request");
    geometryPass = true; run();
    ok &= check(handoffCalls == 1 && collectCalls == 0 && grants == 0 && pickup::queryScope == nullptr,
        "complete query requests before grant, without granting or leaking scope");
    ok &= check(pickup::pending(manager.data(), networkToken, GetTickCount64()), "eligible local pickup lease active");
    SetLastError(123);
    ok &= check(!pickup::transfer(manager.data(), networkToken, 0) && transferCalls == 0
        && GetLastError() == 123 && pickup::deferred == 1 && pickup::activeCalls == 0,
        "local in-range pickup defers outgoing handoff without altering native row or error");
    ok &= check(row[1] == std::byte{0xFF}, "lease never writes authority or network ownership");
    const auto entry = pickup::leases[simulationHandle & 0x1FFFU];
    ok &= check(pickup::pending(manager.data(), networkToken, entry.time + 249)
        && !pickup::pending(manager.data(), networkToken, entry.time + 250)
        && !pickup::pending(manager.data(), networkToken, entry.time - 1), "lease expires and clock rollback releases it");
    put(f.players.data(), 0x54, UINT32_MAX);
    ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "death immediately invalidates lease");
    put(f.players.data(), 0x54, std::uint32_t{44});
    ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "respawn cannot inherit previous body's lease");
    put(f.players.data(), 0x54, std::uint32_t{42});
    put(f.units.data(), Fixture::index * 0xE0 + 0xC, Fixture::legacy + 0x2000U);
    ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "recycled legacy generation cannot hold ownership");
    put(f.units.data(), Fixture::index * 0xE0 + 0xC, Fixture::legacy);
    put(f.units.data(), Fixture::index * 0xE0 + 0x4C, std::uint32_t{0x1AF9E8BC});
    ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "new scene/root cannot inherit lease");
    put(f.units.data(), Fixture::index * 0xE0 + 0x4C, std::uint32_t{0x7AF9E8BC});
    put(row, 4, simulationHandle + 0x2000U);
    ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "recycled simulation slot rejected");
    put(row, 4, simulationHandle);
    ok &= check(!pickup::pending(manager.data(), networkToken + 0x2000U, entry.time), "stale network token rejected");
    for (const auto owner : {0, 0xFD, 0xFE}) {
        put(row, 1, static_cast<std::uint8_t>(owner));
        ok &= check(!pickup::pending(manager.data(), networkToken, entry.time), "remote or already transferring brick unchanged");
    }
    put(row, 1, std::uint8_t{0xFF});
    eligibleResult = false; run();
    ok &= check(!pickup::pending(manager.data(), networkToken, GetTickCount64()), "native inventory rejection revokes lease");
    SetLastError(123);
    ok &= check(pickup::transfer(manager.data(), networkToken, 0) && transferCalls == 1
        && transferArgsOk && GetLastError() == 987, "unclaimed transfer delegates native once with intact ABI and error");
    eligibleResult = true; run(); geometryPass = false; run();
    ok &= check(!pickup::pending(manager.data(), networkToken, GetTickCount64()), "leaving pickup geometry revokes lease");
    geometryPass = true;
    reset(); run(1); run(0, 1); run(0, 0, false);
    ok &= check(handoffCalls == 0 && !pickup::pending(manager.data(), networkToken, GetTickCount64()),
        "remote players, bypassed geometry and preview queries cannot claim");
    f.authority(true); run();
    ok &= check(handoffCalls == 0 && pickup::pending(manager.data(), networkToken, GetTickCount64()),
        "local ownership arrival still protects pickup from immediate outgoing transfer");
    pickup::enabled = false; SetLastError(123);
    ok &= check(pickup::transfer(manager.data(), networkToken, 0), "disabled arbitration passes through");
    pickup::enabled = true; f.authority(false);
    return ok;
}
int main(int argc, char** argv) {
    Fixture f;
    if (!check(f.image != nullptr, "fixture allocated")) return 1;
    pickup::imageBase = reinterpret_cast<std::uintptr_t>(f.image);
    pickup::nativeEligible = &eligible; pickup::nativeHandoff = &handoff;
    pickup::original = &collect; pickup::enabled = true;
    bool ok = check(f.prepared(), "captured eligible dropped-special layout accepted");
    ok &= check(!f.prepared(1), "remote player's interaction cannot claim pickup");
    ok &= check(!f.prepared(2) && !f.prepared(UINT32_MAX), "out-of-range player rejected");
    f.authority(true); ok &= check(!f.prepared(), "already owned pickup remains native"); f.authority(false);
    put(f.players.data(), 0x54, UINT32_MAX); ok &= check(!f.prepared(), "dead player rejected");
    put(f.players.data(), 0x54, std::uint32_t{42});
    for (const auto mask : {0, 1, 3, 4, 6, 255}) {
        put(f.brick.data(), 0x2D0, static_cast<std::uint8_t>(mask));
        ok &= check(!f.prepared(), "primary/heavy/mixed pickups unchanged");
    }
    put(f.brick.data(), 0x2D0, std::uint8_t{2});
    put(f.brick.data(), 0x2DC, std::uint8_t{0}); ok &= check(!f.prepared(), "authored pickups unchanged");
    put(f.brick.data(), 0x2DC, std::uint8_t{1});
    put(f.brick.data(), 0x2DD, std::uint8_t{6}); ok &= check(!f.prepared(), "mixed supported types rejected");
    put(f.brick.data(), 0x2DD, std::uint8_t{2});
    for (const auto quantity : {0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()}) {
        put(f.brick.data(), 0x2D8, quantity); ok &= check(!f.prepared(), "invalid quantity rejected");
    }
    put(f.brick.data(), 0x2D8, 0.024193547666072845F);
    put(f.units.data(), Fixture::index * 0xE0 + 0xC, Fixture::legacy + 0x2000U);
    ok &= check(!f.prepared(), "recycled native slot cannot receive stale request");
    put(f.units.data(), Fixture::index * 0xE0 + 0xC, Fixture::legacy);
    put(f.units.data(), Fixture::index * 0xE0 + 0x4C, UINT32_MAX);
    ok &= check(!f.prepared(), "deleted native object rejected");
    put(f.units.data(), Fixture::index * 0xE0 + 0x4C, std::uint32_t{0x7AF9E8BC});
    put(f.image, 0x1F90E20, std::uint32_t{0x480});
    ok &= check(!f.prepared(), "wrong player stride rejected (480 is decimal)");
    put(f.image, 0x1F90E20, std::uint32_t{480});
    std::uint32_t token{};
    ok &= check(!pickup::prepare(reinterpret_cast<void*>(1), 0, pickup::imageBase, token), "unreadable component fails closed");

    reset(); eligibleResult = false;
    ok &= check(!pickup::request(f.brick.data(), 0, 1000) && eligibleCalls == 1 && handoffCalls == 0,
        "native ammo-full/wrong-weapon decision prevents handoff");
    eligibleResult = true;
    const auto saved = f.brick;
    ok &= check(pickup::request(f.brick.data(), 0, 1000) && handoffCalls == 1 && requested == Fixture::legacy,
        "eligible blocked pickup requests exact full handle");
    ok &= check(!pickup::request(f.brick.data(), 0, 1249) && pickup::request(f.brick.data(), 0, 1250)
        && handoffCalls == 2 && saved == f.brick, "bounded retries do not alter quantity or component");
    ok &= check(pickup::due(Fixture::legacy + 0x2000U, 1251), "new generation does not inherit prior retry delay");
    ok &= check(pickup::due(Fixture::legacy + 0x2000U, 1), "clock rollback cannot suppress retries indefinitely");
    ok &= check(!pickup::due(UINT32_MAX, 2) && !pickup::due(0x1FFF, 2), "invalid retry indices rejected");
    reset();
    SetLastError(123); pickup::collect(f.brick.data(), 0);
    ok &= check(handoffCalls == 1 && collectCalls == 1 && grants == 0 && seenComponent == f.brick.data()
        && seenPlayer == 0 && seenError == 123 && GetLastError() == 321 && pickup::activeCalls == 0,
        "replacement preserves ABI/error and delegates blocked grant exactly once");
    f.authority(true); grantAllowed = true;
    pickup::collect(f.brick.data(), 0); pickup::collect(f.brick.data(), 0);
    ok &= check(handoffCalls == 1 && collectCalls == 3 && grants == 1,
        "after native ownership arrives only native grant controls consumption");
    pickup::enabled = false; f.authority(false);
    pickup::collect(f.brick.data(), 0);
    ok &= check(handoffCalls == 1 && collectCalls == 4, "disabled hook is exact passthrough");
    pickup::enabled = true; pickup::nativeHandoff = nullptr;
    pickup::collect(f.brick.data(), 0);
    ok &= check(handoffCalls == 1 && collectCalls == 5, "unavailable handoff cannot suppress original");
    pickup::nativeHandoff = &handoff;
    ok &= lease_contract(f);
    if (argc == 4) ok &= native_contract(f, argv[1], argv[2], argv[3]);
    else if (argc != 1) return 2;
    if (ok) std::puts(argc == 4 ? "special ammo handoff: PASS (including captured native branch)"
                              : "special ammo handoff: PASS (unit fixtures)");
    return ok ? 0 : 1;
}
