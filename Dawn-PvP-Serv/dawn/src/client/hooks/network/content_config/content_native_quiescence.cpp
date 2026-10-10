#include "content_native_quiescence.h"
#include <cstring>
#include <span>
#include "../../../../core/logging/log.h"
#include "../../../patterns/image_scan.h"
#include "../../../process/freeze/client_process_freeze.h"
namespace dawn::client::hooks::network::content_config::native_work {
namespace {
constexpr std::size_t kFrameLimit = 256;
constexpr auto kFetch = patterns::signature<patterns::signature_length(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 41 0F B6 F8 48 8B DA 48 8B F1 E8 ? ? ? ? BA 19 00 00 00 B9 48 F8 14 00")>(
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 41 0F B6 F8 48 8B DA 48 8B F1 E8 ? ? ? ? BA 19 00 00 00 B9 48 F8 14 00");
constexpr auto kParse = patterns::signature<patterns::signature_length(
    "40 55 48 8D AC 24 50 F4 FF FF 48 81 EC B0 0C 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 A0 0B 00 00 83 3D ? ? ? ? 00 0F 85")>(
    "40 55 48 8D AC 24 50 F4 FF FF 48 81 EC B0 0C 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 A0 0B 00 00 83 3D ? ? ? ? 00 0F 85");
constexpr auto kState = patterns::signature<patterns::signature_length(
    "8B 05 ? ? ? ? C3 ? ? ? ? ? ? ? ? ? 48 83 EC 28 B8 FF FF 00 00 C7 05 ? ? ? ? 01 00 00 00 B9 18 00 00 00 66 89 05")>(
    "8B 05 ? ? ? ? C3 ? ? ? ? ? ? ? ? ? 48 83 EC 28 B8 FF FF 00 00 C7 05 ? ? ? ? 01 00 00 00 B9 18 00 00 00 66 89 05");
constexpr auto kTick = patterns::signature<patterns::signature_length(
    "4C 8B DC 55 56 57 49 8D AB 88 F9 FF FF 48 81 EC 60 07 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 06 00 00 48 8B F9 49 89 5B 10")>(
    "4C 8B DC 55 56 57 49 8D AB 88 F9 FF FF 48 81 EC 60 07 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 06 00 00 48 8B F9 49 89 5B 10");
bool readable(std::uintptr_t address, std::size_t size, void* allocation = nullptr) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    return address != 0 && size != 0 && address + size >= address
        && VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof memory) == sizeof memory
        && memory.State == MEM_COMMIT && (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0
        && (!allocation || memory.AllocationBase == allocation)
        && address + size <= reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
}
bool copy(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!readable(address, size)) return false;
    __try { std::memcpy(output, reinterpret_cast<void*>(address), size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t relative(std::byte* code, std::size_t displacement, std::size_t end) noexcept {
    std::int32_t value{};
    return copy(reinterpret_cast<std::uintptr_t>(code) + displacement, &value, sizeof value)
        ? reinterpret_cast<std::uintptr_t>(code) + end + value : 0;
}
bool bytes(std::byte* code, std::size_t offset, std::span<const unsigned char> expected) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(code) + offset;
    if (!readable(address, expected.size())) return false;
    __try { return std::memcmp(reinterpret_cast<void*>(address), expected.data(), expected.size()) == 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool pending(const Metadata& metadata, bool& valid) noexcept {
    LONG state{};
    valid = copy(reinterpret_cast<std::uintptr_t>(metadata.state), &state, sizeof state);
    // 0 covers queued download, completed-but-unparsed content and active parsing
    return valid && state == 0;
}
enum class TableResult { invalid, function, leaf };
// read only the image's static PE exception table; RtlLookupFunctionEntry can run callbacks
// whose locks belong to a suspended thread
TableResult static_function(DWORD64 pc, DWORD64& base, PRUNTIME_FUNCTION& function) noexcept {
    base = 0; function = nullptr;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<void*>(pc), &memory, sizeof memory) != sizeof memory
        || memory.Type != MEM_IMAGE || memory.State != MEM_COMMIT) return TableResult::invalid;
    base = reinterpret_cast<DWORD64>(memory.AllocationBase);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!copy(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < 0 || dos.e_lfanew > 0x100000
        || !copy(base + static_cast<unsigned>(dos.e_lfanew), &nt, sizeof nt)
        || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION
        || pc < base || pc - base >= nt.OptionalHeader.SizeOfImage) return TableResult::invalid;
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!directory.Size && !directory.VirtualAddress) return TableResult::leaf;
    if (directory.Size % sizeof(RUNTIME_FUNCTION) != 0 || !directory.VirtualAddress
        || directory.VirtualAddress >= nt.OptionalHeader.SizeOfImage
        || directory.Size > nt.OptionalHeader.SizeOfImage - directory.VirtualAddress) return TableResult::invalid;
    std::size_t low{}, high = directory.Size / sizeof(RUNTIME_FUNCTION);
    const auto relativePc = pc - base;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        const auto address = base + directory.VirtualAddress + middle * sizeof(RUNTIME_FUNCTION);
        RUNTIME_FUNCTION record{};
        if (!copy(address, &record, sizeof record) || record.BeginAddress >= record.EndAddress
            || record.EndAddress > nt.OptionalHeader.SizeOfImage
            || record.UnwindData >= nt.OptionalHeader.SizeOfImage) return TableResult::invalid;
        if (relativePc < record.BeginAddress) high = middle;
        else if (relativePc >= record.EndAddress) low = middle + 1;
        else { function = reinterpret_cast<PRUNTIME_FUNCTION>(address); return TableResult::function; }
    }
    return TableResult::leaf;
}
bool unwind(CONTEXT& context, const Metadata& metadata) noexcept {
    __try {
        DWORD64 base{};
        PRUNTIME_FUNCTION function{};
        const auto table = static_function(context.Rip, base, function);
        if (table == TableResult::invalid) return false;
        if (table == TableResult::function) {
            PVOID handlerData{}; DWORD64 establisher{};
            (void)RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function,
                                 &context, &handlerData, &establisher, nullptr);
        } else {
            // VM/private executable code and unregistered nonleaf game code can't use this fallback;
            // only known Windows ABI leaf-stub images are admitted
            bool trusted{};
            for (const auto image : metadata.trustedLeafImages)
                trusted = trusted || (image && reinterpret_cast<DWORD64>(image) == base);
            if (!trusted) return false;
            DWORD64 caller{};
            if (!copy(context.Rsp, &caller, sizeof caller)) return false;
            context.Rip = caller; context.Rsp += sizeof caller;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
bool resolve(std::byte* gate, Metadata& output) noexcept {
    output = {};
    auto* fetch = patterns::scan_main_image_unique(kFetch, "content_native_fetch_ownership");
    auto* parse = patterns::scan_main_image_unique(kParse, "content_native_parse_ownership");
    auto* getter = patterns::scan_main_image_unique(kState, "content_native_state_ownership");
    auto* tick = patterns::scan_main_image_unique(kTick, "content_native_tick_ownership");
    if (!fetch || !parse || !getter || !tick || !gate) return false;
    constexpr unsigned char fetchEnd[]{0x48,0x8b,0x5c,0x24,0x50,0x48,0x8b,0x74,0x24,0x58,0x48,0x83,0xc4,0x40,0x5f,0xc3};
    constexpr unsigned char parseEnd[]{0x48,0x81,0xc4,0xb0,0x0c,0,0,0x5d,0xc3};
    constexpr unsigned char fetchStore[]{0xc7,0x05};
    constexpr unsigned char gateCompare[]{0x80,0x3d};
    constexpr unsigned char tickEnd[]{0x48,0x81,0xc4,0x60,0x07,0,0,0x5f,0x5e,0x5d,0xc3};
    constexpr unsigned char call[]{0xe8}, jump[]{0xe9};
    const auto state = relative(getter, 2, 6);
    if (!state || state % alignof(LONG) != 0 || !readable(state, sizeof(LONG))
        || state != relative(parse, 36, 41) || state != relative(fetch, 54, 62)
        || reinterpret_cast<std::uintptr_t>(gate) != relative(parse, 0xb9, 0xbe)
        || !bytes(fetch, 52, fetchStore) || !bytes(parse, 0xb7, gateCompare)
        || !bytes(fetch, 0x9a, fetchEnd) || !bytes(parse, 0x571, parseEnd)
        || !bytes(tick, 0x6a1, tickEnd) || !bytes(tick, 0x1e3, call)
        || relative(tick,0x1e4,0x1e8) != reinterpret_cast<std::uintptr_t>(fetch)
        || !bytes(tick,0x71b,jump) || relative(tick,0x71c,0x720) != reinterpret_cast<std::uintptr_t>(tick)+0x341) return false;
    output = {reinterpret_cast<const LONG*>(state), reinterpret_cast<std::uintptr_t>(fetch),
              reinterpret_cast<std::uintptr_t>(fetch)+0xaa, reinterpret_cast<std::uintptr_t>(parse),
              reinterpret_cast<std::uintptr_t>(parse)+0x57a, reinterpret_cast<std::uintptr_t>(tick),
              reinterpret_cast<std::uintptr_t>(tick)+0x720};
    output.trustedLeafImages = {GetModuleHandleW(L"ntdll.dll"), GetModuleHandleW(L"kernel32.dll"),
                               GetModuleHandleW(L"kernelbase.dll"), GetModuleHandleW(L"win32u.dll")};
    return true;
}
bool stack_is_idle(CONTEXT context, const Metadata& metadata) noexcept {
    if (!metadata.state || metadata.fetchBegin >= metadata.fetchEnd
        || metadata.parseBegin >= metadata.parseEnd) return false;
    MEMORY_BASIC_INFORMATION stack{};
    if (VirtualQuery(reinterpret_cast<void*>(context.Rsp), &stack, sizeof stack) != sizeof stack) return false;
    for (std::size_t frame = 0; frame < kFrameLimit; ++frame) {
        if (context.Rip == 0) return true;
        if ((context.Rip >= metadata.fetchBegin && context.Rip < metadata.fetchEnd)
            || (context.Rip >= metadata.parseBegin && context.Rip < metadata.parseEnd)
            || (context.Rip >= metadata.tickBegin && context.Rip < metadata.tickEnd)) return false;
        MEMORY_BASIC_INFORMATION code{};
        if (!readable(context.Rsp, sizeof(DWORD64), stack.AllocationBase)
            || VirtualQuery(reinterpret_cast<void*>(context.Rip), &code, sizeof code) != sizeof code
            || code.State != MEM_COMMIT || (code.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
            || (code.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) return false;
        const auto previous = context.Rsp;
        if (!unwind(context, metadata) || context.Rsp <= previous) return false;
    }
    return false;
}
bool restore_when_idle(const Metadata& metadata, gate::Ownership& ownership) noexcept {
    bool valid{};
    if (pending(metadata, valid) || !valid) {
        core::log::write(core::log::Channel::client,core::log::Level::warn,
            valid ? "ev=content stage=shutdown result=retained reason=pending_content"
                  : "ev=content stage=shutdown result=retained reason=invalid_native_state");
        return false;
    }
    process::freeze::Held held{};
    if (!process::freeze::hold(held)) {
        core::log::write(core::log::Channel::client,core::log::Level::warn,
            "ev=content stage=shutdown result=retained reason=freeze_failed");
        return false;
    }
    bool idle = !pending(metadata, valid) && valid;
    CONTEXT context{};
    RtlCaptureContext(&context);
    idle = idle && stack_is_idle(context, metadata);
    for (std::size_t index = 0; idle && index < held.count; ++index) {
        context = {}; context.ContextFlags = CONTEXT_FULL;
        idle = GetThreadContext(held.handles[index], &context) != FALSE && stack_is_idle(context, metadata);
    }
    const bool restored = idle && gate::restore(ownership);
    process::freeze::release(held);
    if (!restored) core::log::write(core::log::Channel::client,core::log::Level::warn,
        "ev=content stage=shutdown result=retained reason=native_work_or_unverified_stack");
    return restored;
}
}
