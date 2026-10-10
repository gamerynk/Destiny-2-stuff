#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"
#include "internal.h"

namespace dawn::client::hooks::queuez {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

// update-notification handler, reached only through an indirect dispatch so it has no call
// site to anchor on; matched on prologue, stack-cookie store and first message read
constexpr std::string_view kHandlerSignatureText =
    "40 53 B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 41 "
    "8B 08";
constexpr auto kHandlerSignature =
    signature<signature_length(kHandlerSignatureText)>(kHandlerSignatureText);

struct MessageLayout {
    static constexpr std::size_t id = 0;
    // payload pointer; the handler reads through it with no null check
    static constexpr std::size_t payload = 8;
};

constexpr char kNotHandled = 0;

using Handler = char(__fastcall*)(void*, void*, std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<Handler> g_original{nullptr};
std::atomic_bool g_reported{false};
std::atomic_uint64_t g_processedUpdateGeneration{0};
struct PreserveLastError {
    DWORD value{GetLastError()};
    ~PreserveLastError() { SetLastError(value); }
};
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
std::atomic_uint64_t g_nullPayloadCount{0};
// first sixteen failures show startup ordering; later powers of two show late failures,
// at most 64 traces per install
constexpr bool trace_null_count(std::uint64_t count) noexcept {
    return count != 0 && (count <= 16
        || (count <= (std::uint64_t{1} << 52) && (count & (count - 1)) == 0));
}

struct PendingOwnerTrace {
    bool available{};
    bool statusKnown{};
    unsigned char status{};
    std::uintptr_t locator{}, callbackRva{};
    const char* reason{"frame_unavailable"};
    bool slotKnown{}, slotPresent{}, nestedAvailable{};
    std::uintptr_t nestedCallbackRva{};
    const char* nestedReason{"not_selected"};
};

// diagnostic snapshots only; native lookup and virtual completion still run themselves.
// ReadProcessMemory and a fixed read budget refuse missing memory without dereferencing it
template<bool ObserveNested, class Reader, class CodeValidator>
PendingOwnerTrace pending_owner_snapshot(std::uintptr_t image, std::size_t imageSize,
    std::uintptr_t pendingAddress, std::uintptr_t descriptor, Reader&& read,
    CodeValidator&& code) noexcept {
    PreserveLastError preserve{};
    PendingOwnerTrace result{};
    struct ReadRecord {
        std::uintptr_t address{};
        std::size_t size{};
        std::array<std::byte, 64> bytes{};
    };
    std::array<ReadRecord, 64> records{};
    std::size_t count{}, reads{};
    const auto bounded_read = [&](std::uintptr_t address, void* output, std::size_t size) noexcept {
        if (reads == 128) return false;
        ++reads;
        return read(address, output, size);
    };
    const auto capture = [&](std::uintptr_t address, void* output, std::size_t size) noexcept {
        if (address == 0 || size == 0 || size > 64 || count == records.size()
            || address > UINTPTR_MAX - size) return false;
        auto& record = records[count];
        if (!bounded_read(address, record.bytes.data(), size)) return false;
        record.address = address; record.size = size; ++count;
        std::memcpy(output, record.bytes.data(), size);
        return true;
    };
    const auto stable = [&]() noexcept {
        for (std::size_t index = 0; index < count; ++index) {
            std::array<std::byte, 64> after{};
            const auto& record = records[index];
            if (!bounded_read(record.address, after.data(), record.size)
                || std::memcmp(after.data(), record.bytes.data(), record.size) != 0) return false;
        }
        return true;
    };
    const auto unavailable = [&](const char* reason) noexcept {
        result.available = false; result.statusKnown = false;
        result.locator = 0; result.callbackRva = 0; result.reason = reason;
        result.slotKnown = false; result.slotPresent = false; result.nestedAvailable = false;
        result.nestedCallbackRva = 0; result.nestedReason = "outer_unavailable";
        return result;
    };
    if (image == 0 || imageSize < 0x2439C78 || image > UINTPTR_MAX - imageSize
        || pendingAddress > UINTPTR_MAX - 8 || descriptor > UINTPTR_MAX - 16)
        return unavailable("bounds");
    constexpr std::array<unsigned char, 12> kLookup{
        0x48,0x83,0xEC,0x08,0x44,0x8B,0x51,0x04,0x4C,0x8B,0xCA,0xC7};
    constexpr std::array<unsigned char, 17> kCompletion{
        0x49,0x8D,0x8D,0x80,0,0,0,0x48,0x8D,0x54,0x24,0x34,0xE8,0x62,0xFE,0x54,0xFF};
    std::array<unsigned char, kLookup.size()> lookup{};
    std::array<unsigned char, kCompletion.size()> completion{};
    if (!capture(image + 0x352310, lookup.data(), lookup.size()) || lookup != kLookup
        || !capture(image + 0xE0249D, completion.data(), completion.size()) || completion != kCompletion)
        return unavailable("native_layout");
    std::array<std::uint32_t, 2> pending{};
    if (!capture(pendingAddress, pending.data(), sizeof pending)) return unavailable("pending_unreadable");
    if (pending[1] == UINT32_MAX) return unavailable("no_pending_handle");
    const auto handle = pending[1];
    const auto tableIndex = (handle >> 13) & ((handle & 0x80000000U) ? 0x3FFFU : 0x3FFU);
    const auto elementIndex = handle & 0x1FFFU;
    std::uintptr_t pool{};
    std::array<std::byte, 24> poolData{};
    if (!capture(image + 0x2439C70, &pool, sizeof pool)
        || !capture(pool, poolData.data(), poolData.size())) return unavailable("pool_unreadable");
    std::uintptr_t array{};
    std::uint32_t poolStride{};
    std::memcpy(&array, poolData.data(), sizeof array);
    std::memcpy(&poolStride, poolData.data() + 0x10, sizeof poolStride);
    const auto tableOffset = static_cast<std::uintptr_t>(tableIndex) * 64;
    if (poolStride != 64 || array > UINTPTR_MAX - tableOffset) return unavailable("pool_layout");
    std::array<std::byte, 64> table{};
    if (!capture(array + tableOffset, table.data(), table.size())) return unavailable("table_unreadable");
    std::uintptr_t elements{}, resource{};
    std::uint32_t elementStride{};
    std::int32_t pointerMask{};
    std::memcpy(&elements, table.data() + 8, sizeof elements);
    std::memcpy(&resource, table.data() + 0x10, sizeof resource);
    std::memcpy(&elementStride, table.data() + 0x30, sizeof elementStride);
    std::memcpy(&pointerMask, table.data() + 0x34, sizeof pointerMask);
    std::array<std::byte, 36> resourceData{};
    if (!capture(resource, resourceData.data(), resourceData.size())) return unavailable("resource_unreadable");
    std::uintptr_t header{}, lookupElements{};
    std::uint32_t keyOffset{}, lookupStride{};
    std::memcpy(&header, resourceData.data(), sizeof header);
    std::memcpy(&lookupElements, resourceData.data() + 8, sizeof lookupElements);
    std::memcpy(&keyOffset, resourceData.data() + 0x1C, sizeof keyOffset);
    std::memcpy(&lookupStride, resourceData.data() + 0x20, sizeof lookupStride);
    if (header > UINTPTR_MAX - 0x1E || lookupStride < 16 || lookupStride > 4096
        || elementStride != lookupStride || lookupElements != elements
        || keyOffset > lookupStride - sizeof(std::uint32_t)) return unavailable("resource_layout");
    std::uint16_t elementCount{};
    if (!capture(header + 0x1C, &elementCount, sizeof elementCount)) return unavailable("count_unreadable");
    if (elementCount > 8192 || elementIndex >= elementCount) return unavailable("element_bounds");
    const auto elementOffset = static_cast<std::uintptr_t>(elementIndex) * lookupStride;
    if (elements > UINTPTR_MAX - elementOffset - lookupStride) return unavailable("element_bounds");
    const auto element = elements + elementOffset;
    std::uint32_t key{};
    std::array<std::uintptr_t, 2> data{};
    if (!capture(element + keyOffset, &key, sizeof key) || key != pending[0]) return unavailable("pending_key");
    if (!capture(element, data.data(), sizeof data)) return unavailable("element_unreadable");
    const auto adjustment = data[1] & static_cast<std::uintptr_t>(static_cast<std::intptr_t>(pointerMask));
    if (adjustment > element) return unavailable("owner_bounds");
    std::uintptr_t object{}, vtable{}, callback{};
    if (!capture(element - adjustment, &object, sizeof object)
        || !capture(object, &vtable, sizeof vtable)
        || !capture(vtable, &callback, sizeof callback)) return unavailable("owner_unreadable");
    if (callback < image || callback - image > imageSize - 32) return unavailable("callback_external");
    if (!code(callback)) return unavailable("callback_not_code");
    std::array<std::byte, 32> callbackCode{};
    if (!capture(callback, callbackCode.data(), callbackCode.size())) return unavailable("callback_unreadable");
    std::uintptr_t status{};
    if (descriptor != 0 && capture(descriptor + 8, &status, sizeof status) && status != 0)
        result.statusKnown = capture(status, &result.status, sizeof result.status);
    if constexpr (ObserveNested) {
        if (callback == image + 0x105BE10) {
            // run27 intermediate completion owner: byte+8 selects a slot, key/handle +10/+14 select
            // the next completion; read here before the native callback returns, never via a saved locator
            constexpr std::array<unsigned char, 32> kIntermediate{
                0x4C,0x8B,0xDC,0x55,0x56,0x49,0x8D,0xAB,0x68,0xFC,0xFF,0xFF,
                0x48,0x81,0xEC,0x88,0x04,0,0,0x48,0x8B,0x05,0x5E,0xDC,0x04,0x01,
                0x48,0x33,0xC4,0x48,0x89,0x85};
            constexpr std::array<unsigned char, 6> kSlot{0x80,0x7E,0x08,0xFF,0x74,0x54};
            constexpr std::array<unsigned char, 14> kNested{
                0x48,0x8D,0x4E,0x10,0x48,0x8D,0x54,0x24,0x30,0xE8,0x3B,0x62,0x2F,0xFF};
            std::array<unsigned char, kSlot.size()> slotCode{};
            std::array<unsigned char, kNested.size()> nestedCode{};
            unsigned char slot{};
            if (std::memcmp(callbackCode.data(), kIntermediate.data(), kIntermediate.size()) != 0
                || !capture(image + 0x105C06D, slotCode.data(), slotCode.size()) || slotCode != kSlot
                || !capture(image + 0x105C0C7, nestedCode.data(), nestedCode.size()) || nestedCode != kNested) {
                result.nestedReason = "native_layout";
            } else if (object > UINTPTR_MAX - 0x18
                || !capture(object + 8, &slot, sizeof slot)) {
                result.nestedReason = "owner_fields_unreadable";
            } else {
                result.slotKnown = true;
                result.slotPresent = slot != 0xFF;
                const auto nested = pending_owner_snapshot<false>(image, imageSize, object + 0x10, 0,
                    [&](std::uintptr_t address, void* output, std::size_t size) noexcept {
                        return capture(address, output, size);
                    }, code);
                result.nestedAvailable = nested.available;
                result.nestedCallbackRva = nested.callbackRva;
                result.nestedReason = nested.reason;
            }
        }
    }
    if (!stable()) return unavailable("snapshot_changed");
    result.available = true; result.locator = object;
    result.callbackRva = callback - image; result.reason = "available";
    return result;
}

template<class Reader, class CodeValidator>
PendingOwnerTrace pending_owner_trace(std::uintptr_t image, std::size_t imageSize,
    std::uintptr_t connection, std::uintptr_t descriptor, Reader&& read, CodeValidator&& code) noexcept {
    PreserveLastError preserve{};
    if (connection == 0 || connection > UINTPTR_MAX - 0x88) {
        PendingOwnerTrace result{}; result.reason = "bounds";
        return result;
    }
    return pending_owner_snapshot<true>(image, imageSize, connection + 0x80, descriptor,
        read, code);
}
#endif

void report_null_producer(const void* context, const std::byte* message,
    [[maybe_unused]] std::uint64_t nullCount = 0) noexcept {
    PreserveLastError preserve{};
    std::uint32_t messageId{};
    if (message != nullptr) {
        std::memcpy(&messageId, message + MessageLayout::id, sizeof messageId);
    }
    // the native response emitter keeps RBX=pending request code, RDI=decoded source; read them
    // from Windows unwind metadata, never by guessing our stack-frame layout
    std::uint32_t contextType{};
    std::uint16_t internalService{};
    std::uint32_t responseContextIndex{}, wireBytes{}, codecService{}, webServiceOpcode{};
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    PendingOwnerTrace ownerTrace{};
#endif
    bool responseFrame{};
    std::int32_t sourceCount{-1};
    bool responseEmitter{};
    const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto read = [](const void* address, void* output, std::size_t bytes) noexcept {
        SIZE_T received{};
        return address != nullptr && ReadProcessMemory(GetCurrentProcess(), address, output,
            bytes, &received) != FALSE && received == bytes;
    };
    (void)read(context, &contextType, sizeof contextType);
    constexpr std::array<unsigned char, 18> kResponseEmitterProlog{
        0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,
        0x48,0x8b,0xec,0x48};
    std::array<unsigned char, kResponseEmitterProlog.size()> actual{};
    if (read(reinterpret_cast<void*>(image + 0xB5DC00), actual.data(), actual.size())
        && actual == kResponseEmitterProlog) {
        CONTEXT unwind{};
        RtlCaptureContext(&unwind);
        for (unsigned depth = 0; depth < 24 && unwind.Rip != 0; ++depth) {
            if (unwind.Rip >= image + 0xB5DC00 && unwind.Rip < image + 0xB5DD17) {
                internalService = static_cast<std::uint16_t>(unwind.Rbx);
                responseEmitter = read(reinterpret_cast<void*>(unwind.Rdi + 0x18),
                    &sourceCount, sizeof sourceCount);
            }
            if (unwind.Rip == image + 0xE0249D) {
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
                IMAGE_DOS_HEADER dos{};
                IMAGE_NT_HEADERS64 nt{};
                if (read(reinterpret_cast<void*>(image), &dos, sizeof dos)
                    && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= sizeof dos
                    && dos.e_lfanew < 0x100000
                    && read(reinterpret_cast<void*>(image + dos.e_lfanew), &nt, sizeof nt)
                    && nt.Signature == IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                    ownerTrace = pending_owner_trace(image, nt.OptionalHeader.SizeOfImage,
                        unwind.R13, unwind.R12,
                        [&](std::uintptr_t address, void* output, std::size_t size) noexcept {
                            return read(reinterpret_cast<void*>(address), output, size);
                        }, [&](std::uintptr_t address) noexcept {
                            MEMORY_BASIC_INFORMATION memory{};
                            if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof memory) != sizeof memory
                                || memory.AllocationBase != reinterpret_cast<void*>(image)
                                || memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE
                                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
                            const DWORD protection = memory.Protect & 0xFF;
                            return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
                                || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
                        });
                } else ownerTrace.reason = "image_unavailable";
#endif
                std::uintptr_t codecVtable{};
                if (read(reinterpret_cast<void*>(unwind.Rbx), &codecVtable, sizeof codecVtable)
                    && codecVtable == image + 0x1C2DED8) {
                    constexpr std::array<unsigned char, 6> kCodecId{0xB8,0x0B,0,0,0,0xC3};
                    std::array<unsigned char, kCodecId.size()> id{};
                    if (read(reinterpret_cast<void*>(image + 0xDFFFE0), id.data(), id.size()) && id == kCodecId) {
                        codecService = 11;
                        std::array<unsigned char, 2> header{};
                        if (read(reinterpret_cast<void*>(unwind.Rdi), header.data(), header.size()))
                            webServiceOpcode = (static_cast<std::uint32_t>(header[0]) << 8) | header[1];
                    }
                }
            }
            // E02990 keeps the native response record in RBX; offsets are validated against its consumer
            // instructions, not inferred from the pending request code
            if (unwind.Rip == image + 0xE02B50) {
                constexpr std::array<unsigned char, 7> kCall{0xE8,0x30,0xF7,0xFF,0xFF,0x84,0xC0};
                std::array<unsigned char, kCall.size()> call{};
                responseFrame = read(reinterpret_cast<void*>(image + 0xE02B4B), call.data(), call.size())
                    && call == kCall
                    && read(reinterpret_cast<void*>(unwind.Rbx + 8), &responseContextIndex, sizeof responseContextIndex)
                    && read(reinterpret_cast<void*>(unwind.Rbx + 0x11C), &wireBytes, sizeof wireBytes);
                break;
            }
            DWORD64 owner{};
            const auto entry = RtlLookupFunctionEntry(unwind.Rip, &owner, nullptr);
            const auto previousStack = unwind.Rsp;
            if (entry != nullptr) {
                PVOID handlerData{};
                DWORD64 establisher{};
                (void)RtlVirtualUnwind(UNW_FLAG_NHANDLER, owner, unwind.Rip, entry, &unwind,
                    &handlerData, &establisher, nullptr);
            } else {
                DWORD64 caller{};
                if (!read(reinterpret_cast<void*>(unwind.Rsp), &caller, sizeof caller)) break;
                unwind.Rip = caller;
                unwind.Rsp += sizeof caller;
            }
            if (unwind.Rsp <= previousStack) break;
        }
    }
    std::array<void*, 20> frames{};
    const USHORT count = CaptureStackBackTrace(1U, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    std::array<char, core::log::kLineCapacity> line{};
    const int initial = std::snprintf(line.data(), line.size(),
        "ev=queuez stage=notification result=skipped reason=null_payload message_id=0x%08X "
        "context_type=0x%08X response_emitter=%u internal_service=%u source_bytes=%d response_frame=%u response_context_index=%u wire_bytes=%u codec_service=%u ws_opcode=%u "
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
        "null_count=%llu pending_owner=%s status_known=%u response_status=%u callback_rva=0x%llX owner_locator=0x%llX "
        "slot_known=%u slot_present=%u nested_owner=%s nested_callback_rva=0x%llX "
#endif
        "stack_rva=",
        messageId, contextType, static_cast<unsigned>(responseEmitter),
        static_cast<unsigned>(internalService), sourceCount, static_cast<unsigned>(responseFrame),
        responseContextIndex, wireBytes, codecService, webServiceOpcode
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
        , static_cast<unsigned long long>(nullCount)
        , ownerTrace.reason, static_cast<unsigned>(ownerTrace.statusKnown), static_cast<unsigned>(ownerTrace.status)
        , static_cast<unsigned long long>(ownerTrace.callbackRva), static_cast<unsigned long long>(ownerTrace.locator)
        , static_cast<unsigned>(ownerTrace.slotKnown), static_cast<unsigned>(ownerTrace.slotPresent)
        , ownerTrace.nestedReason, static_cast<unsigned long long>(ownerTrace.nestedCallbackRva)
#endif
        );
    if (initial <= 0 || static_cast<std::size_t>(initial) >= line.size()) return;
    std::size_t size = static_cast<std::size_t>(initial);
    bool first = true;
    for (USHORT index = 0; index < count; ++index) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(frames[index], &memory, sizeof memory) != sizeof memory
            || memory.AllocationBase != reinterpret_cast<void*>(image)) continue;
        const auto value = reinterpret_cast<std::uintptr_t>(frames[index]);
        const int added = std::snprintf(line.data() + size, line.size() - size, "%s0x%llX",
            first ? "" : ",", static_cast<unsigned long long>(value - image));
        if (added <= 0 || static_cast<std::size_t>(added) >= line.size() - size) break;
        size += static_cast<std::size_t>(added);
        first = false;
    }
    core::log::write(core::log::Channel::client, core::log::Level::warn, {line.data(), size});
}

// native receive emitters leave the descriptor pair zeroed for a non-positive byte count
// but still dispatch it, so the handler reads a missing descriptor; skip only that case
__declspec(noinline) char __fastcall handler(void* self,
                                             void* context,
                                             std::byte* message) noexcept {
    std::uintptr_t payload = 0;
    if (message != nullptr) {
        std::memcpy(&payload, message + MessageLayout::payload, sizeof payload);
    }
    if (payload == 0) {
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
        const auto nullCount = g_nullPayloadCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (trace_null_count(nullCount)) {
            report_null_producer(context, message, nullCount);
        }
#else
        if (!g_reported.exchange(true, std::memory_order_relaxed)) {
            report_null_producer(context, message);
        }
#endif
        return kNotHandled;
    }
    const Handler original = g_original.load(std::memory_order_acquire);
    const char result = original != nullptr ? original(self, context, message) : kNotHandled;
    std::uint32_t messageId = 0;
    if (message != nullptr) {
        std::memcpy(&messageId, message + MessageLayout::id, sizeof messageId);
    }
    // the first field is an internal message identity, not the BAP service number
    const std::uint64_t generation =
        g_processedUpdateGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=queuez stage=native_handler result=%d message_id=%u "
                                    "payload=0x%llX generation=%llu"
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
                                    " null_count=%llu"
#endif
                                    ,
                                    static_cast<int>(result),
                                    messageId,
                                    static_cast<unsigned long long>(payload),
                                    static_cast<unsigned long long>(generation)
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
                                    , static_cast<unsigned long long>(g_nullPayloadCount.load(std::memory_order_relaxed))
#endif
                                    );
    if (count > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return result;
}

} // namespace

bool install_null_payload_guard() noexcept {
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kHandlerSignature, "queuez_update_handler");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=queuez stage=notification result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&handler)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=queuez stage=notification result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<Handler>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=queuez stage=notification result=ok");
    return true;
}

void uninstall_null_payload_guard() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reported.store(false, std::memory_order_release);
    g_processedUpdateGeneration.store(0, std::memory_order_release);
#if defined(RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE) && RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE
    g_nullPayloadCount.store(0, std::memory_order_relaxed);
#endif
}

bool null_payload_guard_installed() noexcept {
    return g_handle.attached;
}

std::uint64_t processed_update_generation() noexcept {
    return g_processedUpdateGeneration.load(std::memory_order_acquire);
}

} // namespace dawn::client::hooks::queuez
