#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "prototypes/pvp_native_query_component.h"
namespace candidate = pvp_native_query_component;
struct Memory {
    std::map<std::uintptr_t, std::vector<std::byte>> regions;
    unsigned reads{}, failAt{};
    bool read(std::uintptr_t address, void* destination, std::size_t count) noexcept {
        if (++reads == failAt) return false;
        for (const auto& [start, bytes] : regions)
            if (address >= start && address - start <= bytes.size()
                && count <= bytes.size() - (address - start)) {
                std::memcpy(destination, bytes.data() + address - start, count); return true;
            }
        return false;
    }
    template<class T> void put(std::uintptr_t address, T value) {
        for (auto& [start, bytes] : regions)
            if (address >= start && address - start + sizeof value <= bytes.size()) {
                std::memcpy(bytes.data() + address - start, &value, sizeof value); return;
            }
        std::abort();
    }
};
int main() {
    constexpr std::uintptr_t image = 0x100000000, query = 0x200000000;
    constexpr std::uintptr_t header = 0x300000000, table = 0x300010000, rows = 0x300020000;
    constexpr std::uintptr_t callback = image + 0xE8A870, object = rows + 0x130;
    Memory memory;
    memory.regions[image + 0x2439000].resize(4096);
    memory.regions[query].resize(0x400);
    memory.regions[header].resize(4096);
    memory.regions[table].resize(4096);
    memory.regions[rows].resize(4096);
    memory.put(image + 0x2439C70, header); memory.put(header, table);
    memory.put(table + 8, rows); memory.put(table + 0x30, std::uint32_t{0x100});
    memory.put(table + 0x34, std::uint32_t{0});
    memory.put(query + 0x30, std::uint32_t{0x800001});
    memory.put(query + 0x34, std::uint32_t{6});
    memory.put(query + 0x38, std::uint32_t{1});
    memory.put(query + 0x40, std::uint64_t{0x30});
    memory.put(rows + 0x108, std::uint64_t{0});
    memory.put(object + 4, std::uint32_t{0x80804494});
    memory.put(object + 0x2C, std::uint32_t{0x12345});
    auto read = [&memory](auto address, void* out, auto size) { return memory.read(address, out, size); };
    unsigned checks{};
    auto check = [&checks](bool condition) { ++checks; if (!condition) std::abort(); };
    candidate::Snapshot snapshot{};
    check(candidate::resolve(image, query, callback, read, snapshot));
    check(snapshot.component == object && snapshot.legacy == 0x12345);
    const auto readCount = memory.reads;
    check(candidate::unchanged(image, query, callback, read, snapshot));
    memory.put(query + 0x40, std::uint64_t{0x31});
    check(!candidate::unchanged(image, query, callback, read, snapshot));
    memory.put(query + 0x40, std::uint64_t{0x30});
    memory.put(object + 0x2C, std::uint32_t{0x14345});
    check(!candidate::unchanged(image, query, callback, read, snapshot));
    memory.put(object + 0x2C, std::uint32_t{0x12345});
    check(!candidate::resolve(image, query, callback + 1, read, snapshot));
    const auto expected = snapshot;
    for (unsigned failure = 1; failure <= readCount; ++failure) {
        memory.reads = 0; memory.failAt = failure;
        check(!candidate::resolve(image, query, callback, read, snapshot));
        check(snapshot == expected);
    }
    memory.failAt = 0;
    memory.put(query + 0x38, UINT32_MAX);
    check(!candidate::resolve(image, query, callback, read, snapshot));
    memory.put(query + 0x38, std::uint32_t{1});
    memory.put(table + 0x30, UINT32_MAX);
    check(!candidate::resolve(image, query, callback, read, snapshot));
    memory.put(table + 0x30, std::uint32_t{0x100});
    memory.put(query + 0x40, UINT64_MAX);
    check(!candidate::resolve(image, query, callback, read, snapshot));
    std::printf("PASS %u checked-descriptor prototype assertions; callback selection/lifetime remain external preconditions\n", checks);
}
