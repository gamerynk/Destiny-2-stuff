#include "native_match_ui.h"
#include "native_match_projection.h"
#include "match_panel.h"
#include "../hooking/detour.h"
#include "../../core/logging/log.h"
#include <Windows.h>
#include <atomic>

namespace dawn::client::crucible::native_ui {
namespace {
namespace p = projection;
using Update = void(__fastcall*)(void*) noexcept;
using Timer = std::int64_t*(__fastcall*)(std::int64_t*) noexcept;
using Phase = std::uint32_t(__fastcall*)() noexcept;
using Score = void*(__fastcall*)() noexcept;
std::array<hooking::detour::Handle, 2> handles{};
std::atomic<Update> originalUpdate{};
std::atomic<Timer> originalTimer{};
std::atomic_bool enabled{};
std::atomic<unsigned> activeCalls{};
const std::byte* image{};
SRWLOCK clockLock = SRWLOCK_INIT;
p::Countdown countdown{};
std::atomic<std::uint64_t> projectedSession{};

struct Call {
    Call() noexcept { activeCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~Call() { activeCalls.fetch_sub(1, std::memory_order_acq_rel); }
};
template<class T> bool read(const void* base, std::size_t offset, T& value) noexcept {
    __try {
        if (!base) return false;
        std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<std::size_t N> bool matches(std::size_t offset, const std::array<std::uint8_t, N>& expected) noexcept {
    std::array<std::uint8_t, N> actual{};
    return read(image, offset, actual) && actual == expected;
}
const std::byte* registry_row(std::uint32_t index) noexcept {
    const std::byte* rows{}; std::uint32_t stride{};
    if (index >= 16 || !read(image, 0x1F90E18, rows) || !rows
        || !read(image, 0x1F90E20, stride) || stride != 480) return nullptr;
    return rows + index * stride;
}
std::uint64_t local_key() noexcept {
    for (std::uint32_t i = 0; i < 16; ++i) {
        const auto* row = registry_row(i); std::uint8_t flags{}; std::uint64_t key{};
        if (read(row, 0x40, flags) && (flags & 1) && read(row, 0x18, key) && key) return key;
    }
    return 0;
}
bool snapshot(p::host::View& view, std::uint64_t& received, std::uint64_t now,
              std::uint64_t& local) noexcept {
    if (!try_snapshot(view, received) || !p::fresh(view, received, now)) return false;
    local = local_key();
    return p::member(view, local) != nullptr;
}
const std::byte* resolve(std::uint32_t datum) noexcept {
    if (datum == 0xFFFFFFFF) return nullptr;
    const auto shifted = static_cast<std::int32_t>(datum) >> 13;
    const auto bucket = ((static_cast<std::uint32_t>(shifted) | 0x0FFC0000U) >> 18)
        & static_cast<std::uint16_t>(shifted);
    const std::byte *root{}, *table{}, *rows{};
    std::uint32_t stride{}; std::int32_t mask{}; std::uint64_t fixup{};
    if (bucket >= 4096 || !read(image, 0x2439C70, root) || !read(root, 0, table) || !table) return nullptr;
    const auto* entry = table + bucket * 64;
    if (!read(entry, 8, rows) || !rows || !read(entry, 48, stride) || !stride || stride > 0x100000
        || !read(entry, 52, mask)) return nullptr;
    const auto* row = rows + (datum & 8191U) * static_cast<std::size_t>(stride);
    if (!read(row, 8, fixup)) return nullptr;
    return reinterpret_cast<const std::byte*>(reinterpret_cast<std::uintptr_t>(row)
        - (fixup & static_cast<std::uint64_t>(static_cast<std::int64_t>(mask))));
}
bool columns(p::Columns& result) noexcept {
    const auto* component = reinterpret_cast<Score>(const_cast<std::byte*>(image) + 0xADFD60)();
    std::uint32_t datum{}, table{}, count{};
    if (!read(component, 0x1710, datum)) return false;
    const auto* definitions = resolve(datum);
    if (!read(definitions, 8, table) || !table || table == 0xFFFFFFFF
        || !read(definitions, 0x224, count) || count < 8 || count > 64) return false;
    constexpr std::array<std::size_t, 3> indices{0, 5, 7};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        std::array<std::uint32_t, 3> labels{}; std::uint8_t format{};
        const auto offset = 0x228 + 20 * indices[i];
        if (!read(definitions, offset, labels) || labels[0] != p::kStatKeys[i]
            || !read(definitions, offset + 12, format) || format != 0) return false;
        result[i] = {table, labels[0], table, labels[1] == 0x811C9DC5 ? labels[0] : labels[1],
            table, labels[2], format, {}};
    }
    return true;
}
bool project(void* model, const p::host::View& view, std::uint64_t local, const p::Columns& defs) noexcept {
    // native owns this model on its update thread; reject stale row mappings before writing.
    // access faults during activity teardown leave native in control
    __try { return p::apply({static_cast<std::byte*>(model), p::kModelSize}, view, local, defs); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool place_labels(p::PlaceLabels& result) noexcept {
    const auto* component = reinterpret_cast<Score>(const_cast<std::byte*>(image) + 0xADFD60)();
    std::uint32_t datum{}; std::uint64_t count{}; std::int64_t relative{};
    if (!read(component, 0x1710, datum)) return false;
    const auto* definitions = resolve(datum);
    if (!read(definitions, 0x20, count) || count != 22
        || !read(definitions, 0x28, relative) || relative < 0 || relative > 0x10000) return false;
    const auto* entries = definitions + 0x38 + relative;
    p::PlaceLabels staged{};
    for (std::size_t i = 0; i < staged.size(); ++i) {
        std::array<std::uint32_t, 3> entry{};
        if (!read(entries, (6 + i) * 12, entry) || entry[0] != 0x80B9E378
            || entry[1] != p::kPlaceKeys[i] || entry[2] != 0xFFFFFFFF) return false;
        staged[i] = {entry[0], entry[1]};
    }
    result = staged; return true;
}
bool project_place(void* model, const p::host::View& view, std::uint64_t local,
                   const p::PlaceLabels& labels) noexcept {
    __try { return p::apply_place({static_cast<std::byte*>(model), p::kModelSize}, view, local, labels); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) std::int64_t* __fastcall timer(std::int64_t* output) noexcept {
    Call call;
    const auto native = originalTimer.load(std::memory_order_acquire);
    auto* result = native ? native(output) : output;
    const auto error = GetLastError();
    if (enabled.load(std::memory_order_acquire) && output && result == output) {
        p::host::View view{}; std::uint64_t received{}, local{}, remaining{};
        const auto now = GetTickCount64();
        if (snapshot(view, received, now, local)) {
            const auto phase = reinterpret_cast<Phase>(const_cast<std::byte*>(image) + 0x4FFD10)();
            if (TryAcquireSRWLockExclusive(&clockLock)) {
                const bool valid = phase <= 255 && countdown.sample(view, received, now,
                    static_cast<std::uint8_t>(phase), remaining);
                ReleaseSRWLockExclusive(&clockLock);
                if (valid) *output = static_cast<std::int64_t>(remaining * 673200 / 1000);
            }
        }
    }
    SetLastError(error);
    return result;
}
__declspec(noinline) void __fastcall update(void* model) noexcept {
    Call call;
    p::host::View view{}; std::uint64_t received{}, local{};
    const bool valid = enabled.load(std::memory_order_acquire)
        && snapshot(view, received, GetTickCount64(), local);
    const auto native = originalUpdate.load(std::memory_order_acquire);
    if (native) native(model);
    const auto error = GetLastError();
    std::uint8_t phase{};
    if (valid && read(model, 0x2114, phase) && (phase == 3 || phase == 6 || phase == 7)) {
        p::Columns defs{};
        if (columns(defs) && project(model, view, local, defs)) {
            p::PlaceLabels labels{};
            if (place_labels(labels)) project_place(model, view, local, labels);
            if (projectedSession.exchange(view.key.session, std::memory_order_relaxed) != view.key.session)
            core::log::write(core::log::Channel::client, core::log::Level::info,
                "ev=native_match_ui stage=project result=ok columns=score,kills,deaths timer=native");
        }
    }
    SetLastError(error);
}
}
void install() noexcept {
    if (handles[0].attached) return;
    image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    const bool supported = image
        && matches(0xC77830, std::array<std::uint8_t,14>{0x40,0x53,0x48,0x83,0xEC,0x40,0x48,0x8B,0xD9,0xE8,0xD2,0x22,0x36,0x00})
        && matches(0x4FFDD0, std::array<std::uint8_t,14>{0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0x42,0xF5,0xE5,0xFF})
        && matches(0x4FFD10, std::array<std::uint8_t,13>{0x48,0x83,0xEC,0x28,0xE8,0xD7,0xFC,0xFF,0xFF,0x48,0x85,0xC0,0x74})
        && matches(0xADFD60, std::array<std::uint8_t,11>{0x48,0x83,0xEC,0x38,0x33,0xD2,0x4C,0x8D,0x44,0x24,0x20})
        ;
    if (supported) {
        auto* base = const_cast<std::byte*>(image);
        const std::array<hooking::detour::Spec, 2> specs{{
            {base + 0xC77830, reinterpret_cast<void*>(&update)},
            {base + 0x4FFDD0, reinterpret_cast<void*>(&timer)}}};
        if (hooking::detour::install(specs, handles)) {
            originalUpdate.store(reinterpret_cast<Update>(handles[0].original), std::memory_order_release);
            originalTimer.store(reinterpret_cast<Timer>(handles[1].original), std::memory_order_release);
            enabled.store(true, std::memory_order_release);
        }
    }
    core::log::write(core::log::Channel::client, core::log::Level::info, handles[0].attached
        ? "ev=native_match_ui stage=install result=ok"
        : "ev=native_match_ui stage=install result=disabled reason=unsupported_layout_or_hook");
}
bool uninstall() noexcept {
    enabled.store(false, std::memory_order_release);
    if (handles[0].attached) {
        const std::array<hooking::detour::ProtectedCodeEntry, 2> entries{{
            {reinterpret_cast<void*>(&update)},
            {reinterpret_cast<void*>(&timer)}}};
        if (hooking::detour::uninstall(handles, entries,
            +[]() noexcept { return activeCalls.load(std::memory_order_acquire) == 0; })
            != hooking::detour::UninstallResult::removed) {
            enabled.store(true, std::memory_order_release); return false;
        }
    }
    originalUpdate = nullptr; originalTimer = nullptr;
    countdown = {}; projectedSession = 0; image = nullptr;
    return true;
}
}
