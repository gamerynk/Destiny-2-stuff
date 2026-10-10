#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../../core/logging/log.h"
#include "../../content/handles/handle_resolver.h"
#include "../../content/handles/layout.h"
#include "../../patterns/game.h"
#include "content.h"
#include "relative.h"
#include "resolution/internal.h"

namespace dawn::client::targets::game::content {
namespace {

// rel32 starts 3 bytes into the schema-resolver load instruction
constexpr std::size_t kHandleTablesDisplacementOffset = 3;
// the schema-resolver load instruction is 7 bytes long
constexpr std::size_t kHandleTablesInstructionSize = 7;
// the descriptor family bias is the immediate of the resolver's add instruction
constexpr std::size_t kDescriptorFamilyBiasOffset = 18;
// the object-store getter call starts 22 bytes into the subscription function
constexpr std::size_t kObjectStoreCallOffset = 22;
constexpr std::size_t kNearCallDisplacementOffset = kObjectStoreCallOffset + 1;
constexpr std::size_t kObjectStoreCallEndOffset = kObjectStoreCallOffset + 5;
constexpr std::byte kNearCallOpcode{0xE8};

Targets g_targets;
std::atomic_bool g_resolved{false};
std::atomic_bool g_queuezDescriptorsReported{false};
std::atomic_bool g_family6DescriptorsReported{false};
std::array<std::atomic_bool, 6> g_family6FailureReported{};

constexpr std::size_t kQueuezFamilyCount = 8;
constexpr std::size_t kQueuezFamilyStride = 96;
constexpr std::size_t kQueuezFamilySlotCount = 6;
constexpr std::size_t kQueuezFamilySlotStride = 16;
constexpr std::size_t kQueuezCodeBytes = 128;
constexpr std::size_t kQueuezDescriptorBytes = 160;
constexpr std::size_t kQueuezDefinitionPrefixBytes = 16;
// tag-reflection elements keep the blob's self-relative pointer at byte 72
constexpr std::size_t kSchemaBlobRelativePointerOffset = 72;
// a resolved tag-reflection blob starts with a 32-byte descriptor header
constexpr std::size_t kSchemaBlobHeaderBytes = 32;
// every tag-reflection field descriptor is exactly 40 bytes
constexpr std::size_t kSchemaFieldDescriptorBytes = 40;
// type code and nested handle offsets inside one 40-byte descriptor
constexpr std::size_t kSchemaFieldTypeOffset = 16;
constexpr std::size_t kSchemaFieldNestedHandleOffset = 20;
constexpr std::size_t kSchemaTreeHandleCapacity = 64;
constexpr std::size_t kSchemaFieldCountLimit = 128;
// build-9704 character views, identified by the multiplayer implementation reference
constexpr std::array<std::uint32_t, 4> kCharacterViewSchemaHandles{
    0x80807587U,
    0x80807589U,
    0x8080758DU,
    0x80807588U,
};
constexpr std::size_t kFamily6LookupCapacity = 64;
constexpr std::size_t kFamily6CallerBytesBefore = 96;
constexpr std::size_t kFamily6CallerBytesAfter = 416;
constexpr std::uint32_t kSchemaRecordIndexBits = 13;
constexpr std::uint32_t kSchemaRecordIndexMask = (1U << kSchemaRecordIndexBits) - 1U;
constexpr std::uint32_t kSchemaHandleSignBit = 0x80000000U;
constexpr std::uint32_t kSchemaSignedHighFill =
    ~((std::numeric_limits<std::uint32_t>::max)() >> kSchemaRecordIndexBits);
constexpr std::uint64_t kSchemaTableMaskFill = 0x0FFC0000ULL;
constexpr unsigned kSchemaTableMaskShift = 18;
constexpr std::uint64_t kSchemaMaximumTableIndex = (1ULL << 14U) - 1ULL;

struct QueuezDescriptorProbe final {
    std::uint32_t rowKey{};
    std::uint32_t count{};
    std::uint32_t stride{};
    std::uint32_t handle{};
    std::uintptr_t descriptor{};
    bool occupied{};
};

struct Family6LookupProbe final {
    std::uintptr_t caller{};
    std::uint32_t objectId{};
    std::int32_t result{};
};

SRWLOCK g_family6LookupLock = SRWLOCK_INIT;
std::array<Family6LookupProbe, kFamily6LookupCapacity> g_family6Lookups{};
std::size_t g_family6LookupCount{};

struct SchemaBlobHeader final {
    std::uint64_t descriptorCount{};
    std::array<std::byte, 16> opaque08{};
    std::int32_t arrayLength{};
    std::array<std::byte, 4> opaque1C{};
};

struct SchemaHandleTrace final {
    const char* stage{"input"};
    std::uintptr_t tablesObject{};
    std::uintptr_t tableBase{};
    std::uintptr_t tableAddress{};
    std::uintptr_t recordArray{};
    std::uintptr_t recordAddress{};
    std::uintptr_t correctionSource{};
    std::uintptr_t definition{};
    std::uint64_t tableIndex{};
    std::uint32_t recordIndex{};
    std::uint32_t recordStride{};
    std::int32_t correctionMask{};
};

static_assert(kQueuezFamilySlotCount * kQueuezFamilySlotStride == kQueuezFamilyStride);
static_assert(sizeof(SchemaBlobHeader) == kSchemaBlobHeaderBytes);

[[nodiscard]] bool readable(const void* source, std::size_t size) noexcept {
    if (source == nullptr || size == 0) {
        return false;
    }
    auto cursor = reinterpret_cast<std::uintptr_t>(source);
    if (cursor > (std::numeric_limits<std::uintptr_t>::max)() - size) {
        return false;
    }
    const std::uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region))
                != sizeof(region)
            || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
            return false;
        }
        const auto regionBegin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (regionBegin > (std::numeric_limits<std::uintptr_t>::max)() - region.RegionSize) {
            return false;
        }
        const std::uintptr_t regionEnd = regionBegin + region.RegionSize;
        if (regionEnd <= cursor) {
            return false;
        }
        cursor = (std::min)(end, regionEnd);
    }
    return true;
}

[[nodiscard]] bool read_memory([[maybe_unused]] void* context,
                               std::uintptr_t address,
                               std::span<std::byte> output) noexcept {
    if (output.empty() || !readable(reinterpret_cast<const void*>(address), output.size())) {
        return false;
    }
    std::memcpy(output.data(), reinterpret_cast<const void*>(address), output.size());
    return true;
}

template <typename Value>
[[nodiscard]] bool read_value(std::uintptr_t address, Value& value) noexcept {
    return read_memory(nullptr,
                       address,
                       std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

[[nodiscard]] bool trace_schema_handle(std::uintptr_t tablesSlot,
                                       std::uint32_t handle,
                                       SchemaHandleTrace& trace) noexcept {
    using ::dawn::client::content::handles::layout::RecordPrefix;
    using ::dawn::client::content::handles::layout::TableDescriptor;

    trace = {};
    if (tablesSlot == 0 || handle == (std::numeric_limits<std::uint32_t>::max)()) {
        trace.stage = "input";
        return false;
    }
    if (!read_value(tablesSlot, trace.tablesObject)) {
        trace.stage = "tables_slot_read";
        return false;
    }
    if (trace.tablesObject == 0) {
        trace.stage = "tables_object_null";
        return false;
    }
    if (!read_value(trace.tablesObject, trace.tableBase)) {
        trace.stage = "table_base_read";
        return false;
    }
    if (trace.tableBase == 0) {
        trace.stage = "table_base_null";
        return false;
    }

    const std::uint32_t encodedHigh =
        (handle >> kSchemaRecordIndexBits)
        | (((handle & kSchemaHandleSignBit) != 0) ? kSchemaSignedHighFill : 0U);
    const std::uint64_t tableMask =
        (static_cast<std::uint64_t>(encodedHigh) | kSchemaTableMaskFill)
        >> kSchemaTableMaskShift;
    trace.tableIndex = static_cast<std::uint16_t>(encodedHigh) & tableMask;
    trace.recordIndex = handle & kSchemaRecordIndexMask;
    if (trace.tableIndex > kSchemaMaximumTableIndex) {
        trace.stage = "table_index";
        return false;
    }
    if (trace.tableIndex
        > ((std::numeric_limits<std::uintptr_t>::max)() - trace.tableBase)
              / sizeof(TableDescriptor)) {
        trace.stage = "table_address";
        return false;
    }
    trace.tableAddress =
        trace.tableBase + static_cast<std::uintptr_t>(trace.tableIndex) * sizeof(TableDescriptor);

    TableDescriptor table{};
    if (!read_value(trace.tableAddress, table)) {
        trace.stage = "table_descriptor_read";
        return false;
    }
    trace.recordArray = table.recordArray;
    trace.recordStride = table.recordStride;
    trace.correctionMask = table.correctionMask;
    if (trace.recordArray == 0) {
        trace.stage = "record_array_null";
        return false;
    }
    if (trace.recordStride == 0) {
        trace.stage = "record_stride_zero";
        return false;
    }
    const std::uint64_t recordOffset =
        static_cast<std::uint64_t>(trace.recordIndex) * trace.recordStride;
    if (recordOffset > (std::numeric_limits<std::uint32_t>::max)()
        || recordOffset > (std::numeric_limits<std::uintptr_t>::max)() - trace.recordArray) {
        trace.stage = "record_address";
        return false;
    }
    trace.recordAddress = trace.recordArray + static_cast<std::uintptr_t>(recordOffset);

    RecordPrefix record{};
    if (!read_value(trace.recordAddress, record)) {
        trace.stage = "record_prefix_read";
        return false;
    }
    trace.correctionSource = static_cast<std::uintptr_t>(record.correctionSource);
    const std::uint64_t extendedMask =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(trace.correctionMask));
    const std::uint64_t correction = record.correctionSource & extendedMask;
    // the retail resolver uses an unsigned 64-bit SUB here, so a negative self-relative
    // correction wraps by design and lands back inside the main image
    trace.definition = static_cast<std::uintptr_t>(
        static_cast<std::uint64_t>(trace.recordAddress) - correction);
    if (trace.definition == 0) {
        trace.stage = "definition_null";
        return false;
    }
    trace.stage = "ok";
    return true;
}

using QueuezObjectStoreGetter = std::byte* (*)();

[[nodiscard]] std::byte* object_store(std::byte* getter) noexcept {
    if (!readable(getter, 1)) {
        return nullptr;
    }
    __try {
        return reinterpret_cast<QueuezObjectStoreGetter>(getter)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

template <std::size_t Capacity>
void report_bytes(const char* subject,
                  int family,
                  const std::byte* source,
                  std::size_t size) noexcept {
    if (subject == nullptr || !readable(source, size)) {
        return;
    }
    std::array<char, Capacity> line{};
    int written = family >= 0
                      ? std::snprintf(line.data(),
                                      line.size(),
                                      "ev=queuez_descriptor stage=census subject=%s family=%d"
                                      " address=%p bytes=",
                                      subject,
                                      family,
                                      source)
                      : std::snprintf(line.data(),
                                      line.size(),
                                      "ev=queuez_descriptor stage=census subject=%s address=%p bytes=",
                                      subject,
                                      source);
    if (written <= 0 || static_cast<std::size_t>(written) >= line.size()) {
        return;
    }
    std::size_t cursor = static_cast<std::size_t>(written);
    for (std::size_t index = 0; index < size && cursor + 2U < line.size(); ++index) {
        const int count = std::snprintf(line.data() + cursor,
                                        line.size() - cursor,
                                        "%02X",
                                        static_cast<unsigned>(source[index]));
        if (count != 2) {
            return;
        }
        cursor += 2U;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     {line.data(), cursor});
}

[[nodiscard]] bool add_signed_fits(std::uintptr_t base,
                                   std::int64_t displacement,
                                   std::uintptr_t& result) noexcept {
    if (displacement >= 0) {
        const auto positive = static_cast<std::uint64_t>(displacement);
        if (positive > (std::numeric_limits<std::uintptr_t>::max)() - base) {
            return false;
        }
        result = base + static_cast<std::uintptr_t>(positive);
        return true;
    }
    const std::uint64_t magnitude = 0ULL - static_cast<std::uint64_t>(displacement);
    if (magnitude > base) {
        return false;
    }
    result = base - static_cast<std::uintptr_t>(magnitude);
    return true;
}

[[nodiscard]] bool resolve_schema_blob_from_element(
    std::uintptr_t element,
    std::uintptr_t& blob,
    SchemaBlobHeader& header,
    const char** failureStage = nullptr) noexcept {
    blob = 0;
    if (failureStage != nullptr) {
        *failureStage = "relative_pointer_range";
    }
    if (element > (std::numeric_limits<std::uintptr_t>::max)()
                      - kSchemaBlobRelativePointerOffset) {
        return false;
    }
    const std::uintptr_t relativePointer = element + kSchemaBlobRelativePointerOffset;
    std::int64_t displacement{};
    if (!read_memory(nullptr,
                     relativePointer,
                     std::span(reinterpret_cast<std::byte*>(&displacement),
                               sizeof displacement))) {
        if (failureStage != nullptr) {
            *failureStage = "relative_pointer_read";
        }
    } else if (!add_signed_fits(relativePointer, displacement, blob)) {
        if (failureStage != nullptr) {
            *failureStage = "blob_address_range";
        }
    } else if (!read_memory(nullptr,
                            blob,
                            std::span(reinterpret_cast<std::byte*>(&header), sizeof header))) {
        if (failureStage != nullptr) {
            *failureStage = "blob_header_read";
        }
    } else if (header.descriptorCount > kSchemaFieldCountLimit) {
        if (failureStage != nullptr) {
            *failureStage = "field_count_bound";
        }
    } else {
        if (failureStage != nullptr) {
            *failureStage = "ok";
        }
        return true;
    }

    blob = 0;
    header = {};
    return false;
}

[[nodiscard]] bool resolve_schema_blob(
    const ::dawn::client::content::handles::Source& source,
    std::uint32_t handle,
    std::uintptr_t& element,
    std::uintptr_t& blob,
    SchemaBlobHeader& header,
    const char** failureStage = nullptr) noexcept {
    element = 0;
    blob = 0;
    if (failureStage != nullptr) {
        *failureStage = "definition";
    }
    if (!::dawn::client::content::handles::resolve_schema(source, handle, element)) {
        return false;
    }
    return resolve_schema_blob_from_element(element, blob, header, failureStage);
}

void report_schema_tree(
    const ::dawn::client::content::handles::Source& source,
    std::size_t family,
    std::size_t slot,
    std::uint32_t rootHandle) noexcept {
    if (rootHandle == 0 || rootHandle == (std::numeric_limits<std::uint32_t>::max)()) {
        return;
    }
    std::array<std::uint32_t, kSchemaTreeHandleCapacity> handles{};
    handles[0] = rootHandle;
    std::size_t handleCount = 1;
    for (std::size_t handleIndex = 0; handleIndex < handleCount; ++handleIndex) {
        const std::uint32_t handle = handles[handleIndex];
        bool duplicate = false;
        for (std::size_t prior = 0; prior < handleIndex; ++prior) {
            duplicate = duplicate || handles[prior] == handle;
        }
        if (duplicate) {
            continue;
        }

        std::uintptr_t element{};
        std::uintptr_t blob{};
        SchemaBlobHeader header{};
        SchemaHandleTrace trace{};
        const bool sharedResolved =
            ::dawn::client::content::handles::resolve_schema(source, handle, element);
        const bool traced = !sharedResolved
                            && trace_schema_handle(source.tablesSlot, handle, trace)
                            && readable(reinterpret_cast<const void*>(trace.definition),
                                        kQueuezDefinitionPrefixBytes);
        if (traced) {
            element = trace.definition;
        }
        const char* reflectionStage = "definition";
        const bool resolved = (sharedResolved || traced)
                              && resolve_schema_blob_from_element(element,
                                                                  blob,
                                                                  header,
                                                                  &reflectionStage);
        std::array<char, 448> summary{};
        const int summaryCount = std::snprintf(
            summary.data(),
            summary.size(),
            "ev=queuez_schema stage=tree family=%zu slot=%zu handle=0x%08X result=%s"
            " definition_source=%s failure=%s element=%p blob=%p fields=%llu array_len=%d"
            " mutation=observe_only",
            family,
            slot,
            handle,
            resolved ? "ok" : "unresolved",
            sharedResolved ? "shared" : (traced ? "native_trace" : trace.stage),
            resolved ? "none" : reflectionStage,
            reinterpret_cast<void*>(element),
            reinterpret_cast<void*>(blob),
            static_cast<unsigned long long>(header.descriptorCount),
            header.arrayLength);
        if (summaryCount > 0) {
            core::log::write(
                core::log::Channel::client,
                resolved ? core::log::Level::info : core::log::Level::warn,
                {summary.data(),
                 (std::min)(static_cast<std::size_t>(summaryCount), summary.size() - 1U)});
        }
        if (!resolved) {
            continue;
        }

        for (std::size_t field = 0;
             field < static_cast<std::size_t>(header.descriptorCount);
             ++field) {
            if (field > ((std::numeric_limits<std::uintptr_t>::max)()
                         - blob - kSchemaBlobHeaderBytes)
                            / kSchemaFieldDescriptorBytes) {
                break;
            }
            const std::uintptr_t descriptorAddress =
                blob + kSchemaBlobHeaderBytes + field * kSchemaFieldDescriptorBytes;
            const auto* const descriptor =
                reinterpret_cast<const std::byte*>(descriptorAddress);
            if (!readable(descriptor, kSchemaFieldDescriptorBytes)) {
                break;
            }
            std::array<char, 80> subject{};
            (void)std::snprintf(subject.data(),
                                subject.size(),
                                "schema_f%zu_s%zu_h%08X_f%zu",
                                family,
                                slot,
                                handle,
                                field);
            report_bytes<256>(subject.data(),
                              static_cast<int>(family),
                              descriptor,
                              kSchemaFieldDescriptorBytes);

            std::uint8_t type{};
            std::uint32_t nested{};
            std::memcpy(&type, descriptor + kSchemaFieldTypeOffset, sizeof type);
            std::memcpy(&nested,
                        descriptor + kSchemaFieldNestedHandleOffset,
                        sizeof nested);
            if (type == 1 && nested != 0
                && nested != (std::numeric_limits<std::uint32_t>::max)()
                && handleCount < handles.size()) {
                bool alreadyQueued = false;
                for (std::size_t index = 0; index < handleCount; ++index) {
                    alreadyQueued = alreadyQueued || handles[index] == nested;
                }
                if (!alreadyQueued) {
                    handles[handleCount++] = nested;
                }
            }
        }
    }
}

void report_character_view_schema_roots(
    const ::dawn::client::content::handles::Source& source) noexcept {
    for (std::size_t slot = 0; slot < kCharacterViewSchemaHandles.size(); ++slot) {
        const std::uint32_t handle = kCharacterViewSchemaHandles[slot];
        std::uintptr_t element{};
        std::uintptr_t blob{};
        SchemaBlobHeader header{};
        const char* failureStage = "definition";
        const bool resolved = resolve_schema_blob(source,
                                                  handle,
                                                  element,
                                                  blob,
                                                  header,
                                                  &failureStage);
        std::array<char, 384> summary{};
        const int summaryCount = std::snprintf(
            summary.data(),
            summary.size(),
            "ev=character_view_schema stage=root slot=%zu handle=0x%08X result=%s"
            " failure=%s element=%p blob=%p fields=%llu array_len=%d"
            " mutation=observe_only",
            slot,
            handle,
            resolved ? "ok" : "unresolved",
            resolved ? "none" : failureStage,
            reinterpret_cast<void*>(element),
            reinterpret_cast<void*>(blob),
            static_cast<unsigned long long>(header.descriptorCount),
            header.arrayLength);
        if (summaryCount > 0) {
            core::log::write(
                core::log::Channel::client,
                resolved ? core::log::Level::info : core::log::Level::warn,
                {summary.data(),
                 (std::min)(static_cast<std::size_t>(summaryCount), summary.size() - 1U)});
        }
        if (!resolved) {
            continue;
        }

        for (std::size_t field = 0;
             field < static_cast<std::size_t>(header.descriptorCount);
             ++field) {
            const std::uintptr_t descriptorAddress =
                blob + kSchemaBlobHeaderBytes + field * kSchemaFieldDescriptorBytes;
            const auto* const descriptor =
                reinterpret_cast<const std::byte*>(descriptorAddress);
            if (!readable(descriptor, kSchemaFieldDescriptorBytes)) {
                break;
            }
            std::array<char, 80> subject{};
            (void)std::snprintf(subject.data(),
                                subject.size(),
                                "character_view_h%08X_f%zu",
                                handle,
                                field);
            report_bytes<256>(subject.data(),
                              -1,
                              descriptor,
                              kSchemaFieldDescriptorBytes);
        }
    }
}

[[nodiscard]] constexpr std::size_t index(patterns::game::Id id) noexcept {
    return static_cast<std::size_t>(id) - resolution::kContentFirstMatch;
}

} // namespace

bool derive(std::span<const patterns::ImageRange> image,
            std::span<const patterns::Match> matches,
            Targets& output) noexcept {
    output = {};
    if (matches.size() != resolution::kContentMatchCount) {
        return false;
    }

    Targets resolved;
    resolved.getItemStatValue = matches[index(patterns::game::Id::getItemStatValue)].address;
    resolved.lightValueToScalar = matches[index(patterns::game::Id::lightValueToScalar)].address;
    auto* objectResolver = matches[index(patterns::game::Id::queuezObjectResolver)].address;
    auto* family5Subscribe = matches[index(patterns::game::Id::queuezFamily5Subscribe)].address;
    resolved.queuezObjectResolver = objectResolver;
    resolved.queuezFamily5Subscribe = family5Subscribe;
    if (!relative::resolve(objectResolver,
                           kHandleTablesDisplacementOffset,
                           kHandleTablesInstructionSize,
                           resolved.contentHandleTablesSlot)
        || family5Subscribe[kObjectStoreCallOffset] != kNearCallOpcode
        || !relative::resolve(family5Subscribe,
                              kNearCallDisplacementOffset,
                              kObjectStoreCallEndOffset,
                              resolved.queuezObjectStoreGetter)
        || !relative::contains(image, resolved.queuezObjectStoreGetter)) {
        return false;
    }
    std::memcpy(&resolved.queuezDescriptorFamilyBias,
                objectResolver + kDescriptorFamilyBiasOffset,
                sizeof resolved.queuezDescriptorFamilyBias);
    if (resolved.queuezDescriptorFamilyBias == 0) {
        return false;
    }
    output = resolved;
    return true;
}

void publish(const Targets& targets) noexcept {
    g_targets = targets;
    g_queuezDescriptorsReported.store(false, std::memory_order_release);
    g_family6DescriptorsReported.store(false, std::memory_order_release);
    for (auto& reported : g_family6FailureReported) {
        reported.store(false, std::memory_order_release);
    }
    AcquireSRWLockExclusive(&g_family6LookupLock);
    g_family6Lookups = {};
    g_family6LookupCount = 0;
    ReleaseSRWLockExclusive(&g_family6LookupLock);
    g_resolved.store(true, std::memory_order_release);
}

bool resolve(std::span<const patterns::ImageRange> image) noexcept {
    g_resolved.store(false, std::memory_order_release);
    g_targets = {};
    const auto definitions = patterns::game::definitions().subspan(resolution::kContentFirstMatch,
                                                                   resolution::kContentMatchCount);
    std::array<patterns::Match, resolution::kContentMatchCount> matches{};
    if (!patterns::resolve_all(image, definitions, matches)) {
        return false;
    }
    for (const patterns::Match match : matches) {
        if (match.status != patterns::MatchStatus::unique) {
            return false;
        }
    }

    Targets resolved;
    if (!derive(image, matches, resolved)) {
        return false;
    }
    publish(resolved);
    return true;
}

void clear() noexcept {
    g_resolved.store(false, std::memory_order_release);
    g_queuezDescriptorsReported.store(false, std::memory_order_release);
    g_family6DescriptorsReported.store(false, std::memory_order_release);
    for (auto& reported : g_family6FailureReported) {
        reported.store(false, std::memory_order_release);
    }
    AcquireSRWLockExclusive(&g_family6LookupLock);
    g_family6Lookups = {};
    g_family6LookupCount = 0;
    ReleaseSRWLockExclusive(&g_family6LookupLock);
    g_targets = {};
}

const Targets& get() noexcept {
    return g_targets;
}

bool report_queuez_descriptors_once() noexcept {
    if (g_queuezDescriptorsReported.load(std::memory_order_acquire)) {
        return true;
    }
    const Targets targets = g_targets;
    if (!g_resolved.load(std::memory_order_acquire)
        || !readable(targets.contentHandleTablesSlot, sizeof(void*))
        || !readable(targets.queuezObjectStoreGetter, 1)) {
        return false;
    }
    std::byte* const store = object_store(targets.queuezObjectStoreGetter);
    if (store == nullptr || targets.queuezDescriptorFamilyBias > 0x100000U) {
        return false;
    }
    const std::size_t biasedFamily = targets.queuezDescriptorFamilyBias;
    if (biasedFamily > (std::numeric_limits<std::size_t>::max)() / kQueuezFamilyStride
        - kQueuezFamilyCount) {
        return false;
    }
    auto* const first = store + biasedFamily * kQueuezFamilyStride;
    if (!readable(first, kQueuezFamilyCount * kQueuezFamilyStride)
        || !readable(targets.queuezObjectResolver, kQueuezCodeBytes)
        || !readable(targets.queuezFamily5Subscribe, kQueuezCodeBytes)) {
        return false;
    }

    std::array<std::array<QueuezDescriptorProbe, kQueuezFamilySlotCount>, kQueuezFamilyCount>
        probes{};
    std::array<std::size_t, kQueuezFamilyCount> occupiedCounts{};
    std::array<std::size_t, kQueuezFamilyCount> resolvedCounts{};
    const ::dawn::client::content::handles::Source source{
        reinterpret_cast<std::uintptr_t>(targets.contentHandleTablesSlot),
        nullptr,
        &read_memory,
    };
    report_character_view_schema_roots(source);
    for (std::size_t family = 0; family < kQueuezFamilyCount; ++family) {
        auto* const row = first + family * kQueuezFamilyStride;
        for (std::size_t slot = 0; slot < kQueuezFamilySlotCount; ++slot) {
            auto* const record = row + slot * kQueuezFamilySlotStride;
            QueuezDescriptorProbe& probe = probes[family][slot];
            std::memcpy(&probe.rowKey, record, sizeof probe.rowKey);
            std::memcpy(&probe.count, record + 4, sizeof probe.count);
            std::memcpy(&probe.stride, record + 8, sizeof probe.stride);
            std::memcpy(&probe.handle, record + 12, sizeof probe.handle);
            probe.occupied = probe.rowKey != (std::numeric_limits<std::uint32_t>::max)();
            if (!probe.occupied || probe.handle == (std::numeric_limits<std::uint32_t>::max)()) {
                continue;
            }
            ++occupiedCounts[family];
            if (!::dawn::client::content::handles::resolve_schema(source,
                                                                     probe.handle,
                                                                     probe.descriptor)
                || !readable(reinterpret_cast<const void*>(probe.descriptor),
                             kQueuezDescriptorBytes)) {
                probe.descriptor = 0;
                continue;
            }
            ++resolvedCounts[family];
            if (family == 1 && slot <= 3) {
                report_schema_tree(source, family, slot, probe.handle);
            }
        }
    }
    // keep the broad census gated on the established Family-1 row; the Family-6 path below
    // reads only each definition prefix and stays independently retryable
    if (occupiedCounts[1] == 0) {
        return false;
    }

    report_bytes<512>("resolver_code", -1, targets.queuezObjectResolver, kQueuezCodeBytes);
    report_bytes<512>("family5_code", -1, targets.queuezFamily5Subscribe, kQueuezCodeBytes);
    for (std::size_t family = 0; family < kQueuezFamilyCount; ++family) {
        report_bytes<512>("handle_row",
                          static_cast<int>(family),
                          first + family * kQueuezFamilyStride,
                          kQueuezFamilyStride);
        for (std::size_t slot = 0; slot < kQueuezFamilySlotCount; ++slot) {
            const QueuezDescriptorProbe& probe = probes[family][slot];
            if (probe.descriptor == 0) {
                continue;
            }
            std::array<char, 48> subject{};
            (void)std::snprintf(subject.data(), subject.size(), "descriptor_slot_%zu", slot);
            report_bytes<768>(subject.data(),
                              static_cast<int>(family),
                              reinterpret_cast<const std::byte*>(probe.descriptor),
                              kQueuezDescriptorBytes);
        }
    }
    std::array<char, 256> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=queuez_descriptor stage=census result=ok store=%p slot=%p bias=0x%X stride=%zu"
        " family1_occupied=%zu family1_resolved=%zu"
        " family6_occupied=%zu family6_resolved=%zu",
        store,
        targets.contentHandleTablesSlot,
        targets.queuezDescriptorFamilyBias,
        kQueuezFamilyStride,
        occupiedCounts[1],
        resolvedCounts[1],
        occupiedCounts[6],
        resolvedCounts[6]);
    if (count > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
    }
    (void)report_family6_descriptors_once();
    g_queuezDescriptorsReported.store(true, std::memory_order_release);
    return true;
}

bool report_family6_descriptors_once() noexcept {
    if (g_family6DescriptorsReported.load(std::memory_order_acquire)) {
        return true;
    }
    const Targets targets = g_targets;
    if (!g_resolved.load(std::memory_order_acquire)
        || !readable(targets.contentHandleTablesSlot, sizeof(void*))
        || !readable(targets.queuezObjectStoreGetter, 1)) {
        return false;
    }
    std::byte* const store = object_store(targets.queuezObjectStoreGetter);
    if (store == nullptr || targets.queuezDescriptorFamilyBias > 0x100000U) {
        return false;
    }
    const std::size_t biasedFamily = targets.queuezDescriptorFamilyBias;
    constexpr std::size_t family = 6;
    if (biasedFamily > (std::numeric_limits<std::size_t>::max)() / kQueuezFamilyStride
                           - family - 1U) {
        return false;
    }
    auto* const row = store + (biasedFamily + family) * kQueuezFamilyStride;
    if (!readable(row, kQueuezFamilyStride)) {
        return false;
    }
    const ::dawn::client::content::handles::Source source{
        reinterpret_cast<std::uintptr_t>(targets.contentHandleTablesSlot),
        nullptr,
        &read_memory,
    };

    std::array<QueuezDescriptorProbe, kQueuezFamilySlotCount> probes{};
    std::size_t occupied{};
    std::size_t definitions{};
    for (std::size_t slot = 0; slot < probes.size(); ++slot) {
        const auto* const record = row + slot * kQueuezFamilySlotStride;
        QueuezDescriptorProbe& probe = probes[slot];
        std::memcpy(&probe.rowKey, record, sizeof probe.rowKey);
        std::memcpy(&probe.count, record + 4, sizeof probe.count);
        std::memcpy(&probe.stride, record + 8, sizeof probe.stride);
        std::memcpy(&probe.handle, record + 12, sizeof probe.handle);
        probe.occupied = probe.rowKey != (std::numeric_limits<std::uint32_t>::max)();
        if (!probe.occupied) {
            continue;
        }
        ++occupied;

        SchemaHandleTrace trace{};
        const bool traced = trace_schema_handle(source.tablesSlot, probe.handle, trace);
        const bool sharedResolved = probe.handle != 0
                                    && ::dawn::client::content::handles::resolve_schema(
                                        source, probe.handle, probe.descriptor);
        if (!sharedResolved && traced) {
            probe.descriptor = trace.definition;
        }
        const bool definitionResolved = sharedResolved || traced;
        const bool prefixReadable =
            definitionResolved
            && readable(reinterpret_cast<const void*>(probe.descriptor),
                        kQueuezDefinitionPrefixBytes);
        if (!prefixReadable) {
            if (!g_family6FailureReported[slot].exchange(true, std::memory_order_acq_rel)) {
                std::array<char, 1024> failure{};
                const int failureCount = std::snprintf(
                    failure.data(),
                    failure.size(),
                    "ev=queuez_family6 stage=definition result=unavailable slot=%zu"
                    " base=0x%08X count=%u stride=%u handle=0x%08X failure=%s"
                    " resolver_definition=%p trace=%s trace_definition=%p"
                    " tables_slot=%p tables_object=%p table_base=%p table_index=%llu"
                    " table=%p record_index=%u record_array=%p record_stride=%u"
                    " correction_mask=0x%08X record=%p correction_source=0x%llX"
                    " mutation=observe_only",
                    slot,
                    probe.rowKey,
                    probe.count,
                    probe.stride,
                    probe.handle,
                    definitionResolved ? "definition_prefix_read" : "definition_resolve",
                    reinterpret_cast<void*>(probe.descriptor),
                    traced ? "ok" : trace.stage,
                    reinterpret_cast<void*>(trace.definition),
                    reinterpret_cast<void*>(source.tablesSlot),
                    reinterpret_cast<void*>(trace.tablesObject),
                    reinterpret_cast<void*>(trace.tableBase),
                    static_cast<unsigned long long>(trace.tableIndex),
                    reinterpret_cast<void*>(trace.tableAddress),
                    trace.recordIndex,
                    reinterpret_cast<void*>(trace.recordArray),
                    trace.recordStride,
                    static_cast<unsigned>(trace.correctionMask),
                    reinterpret_cast<void*>(trace.recordAddress),
                    static_cast<unsigned long long>(trace.correctionSource));
                if (failureCount > 0) {
                    core::log::write(
                        core::log::Channel::client,
                        core::log::Level::info,
                        {failure.data(),
                         (std::min)(static_cast<std::size_t>(failureCount),
                                    failure.size() - 1U)});
                }
            }
            continue;
        }
        ++definitions;

        std::uint32_t objectId{};
        std::memcpy(&objectId,
                    reinterpret_cast<const void*>(probe.descriptor + 8U),
                    sizeof objectId);
        std::size_t definitionBytes = kQueuezDefinitionPrefixBytes;
        for (const std::size_t candidate : {kQueuezDescriptorBytes,
                                            std::size_t{128},
                                            std::size_t{96},
                                            std::size_t{64},
                                            std::size_t{32}}) {
            if (readable(reinterpret_cast<const void*>(probe.descriptor), candidate)) {
                definitionBytes = candidate;
                break;
            }
        }
        std::array<char, 512> definition{};
        const int definitionCount = std::snprintf(
            definition.data(),
            definition.size(),
            "ev=queuez_family6 stage=definition result=ok slot=%zu base=0x%08X"
            " count=%u stride=%u handle=0x%08X element=%p object_id=0x%08X"
            " readable_bytes=%zu definition_source=%s trace=%s trace_match=%s"
            " mutation=observe_only",
            slot,
            probe.rowKey,
            probe.count,
            probe.stride,
            probe.handle,
            reinterpret_cast<void*>(probe.descriptor),
            objectId,
            definitionBytes,
            sharedResolved ? "shared" : "native_trace",
            traced ? "ok" : trace.stage,
            traced && trace.definition == probe.descriptor ? "yes" : "no");
        if (definitionCount > 0) {
            core::log::write(
                core::log::Channel::client,
                core::log::Level::info,
                {definition.data(),
                 (std::min)(static_cast<std::size_t>(definitionCount),
                            definition.size() - 1U)});
        }
        std::array<char, 64> subject{};
        (void)std::snprintf(subject.data(), subject.size(), "family6_definition_slot_%zu", slot);
        report_bytes<768>(subject.data(),
                          static_cast<int>(family),
                          reinterpret_cast<const std::byte*>(probe.descriptor),
                          definitionBytes);

        std::uintptr_t reflectionElement{};
        std::uintptr_t blob{};
        SchemaBlobHeader header{};
        const char* reflectionStage = "unknown";
        reflectionElement = probe.descriptor;
        const bool reflection = resolve_schema_blob_from_element(reflectionElement,
                                                                 blob,
                                                                 header,
                                                                 &reflectionStage);
        std::array<char, 512> reflectionLine{};
        const int reflectionCount = std::snprintf(
            reflectionLine.data(),
            reflectionLine.size(),
            "ev=queuez_family6 stage=reflection result=%s slot=%zu handle=0x%08X"
            " failure=%s element=%p definition_match=%s blob=%p fields=%llu"
            " array_len=%d mutation=observe_only",
            reflection ? "ok" : "unavailable",
            slot,
            probe.handle,
            reflection ? "none" : reflectionStage,
            reinterpret_cast<void*>(reflectionElement),
            reflection && reflectionElement == probe.descriptor ? "yes" : "no",
            reinterpret_cast<void*>(blob),
            static_cast<unsigned long long>(header.descriptorCount),
            header.arrayLength);
        if (reflectionCount > 0) {
            core::log::write(
                core::log::Channel::client,
                core::log::Level::info,
                {reflectionLine.data(),
                 (std::min)(static_cast<std::size_t>(reflectionCount),
                            reflectionLine.size() - 1U)});
        }
        if (reflection) {
            report_schema_tree(source, family, slot, probe.handle);
        }
    }
    if (occupied == 0 || definitions != occupied) {
        return false;
    }

    std::array<char, 192> complete{};
    const int completeCount = std::snprintf(
        complete.data(),
        complete.size(),
        "ev=queuez_family6 stage=definition result=complete occupied=%zu definitions=%zu"
        " mutation=observe_only",
        occupied,
        definitions);
    if (completeCount > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::info,
            {complete.data(),
             (std::min)(static_cast<std::size_t>(completeCount), complete.size() - 1U)});
    }
    g_family6DescriptorsReported.store(true, std::memory_order_release);
    return true;
}

void report_family6_lookup(std::uint32_t objectId,
                           std::int32_t result,
                           std::uintptr_t caller) noexcept {
    std::size_t sequence{};
    AcquireSRWLockExclusive(&g_family6LookupLock);
    for (std::size_t index = 0; index < g_family6LookupCount; ++index) {
        const Family6LookupProbe& seen = g_family6Lookups[index];
        if (seen.caller == caller && seen.objectId == objectId && seen.result == result) {
            ReleaseSRWLockExclusive(&g_family6LookupLock);
            return;
        }
    }
    if (g_family6LookupCount >= g_family6Lookups.size()) {
        ReleaseSRWLockExclusive(&g_family6LookupLock);
        return;
    }
    sequence = g_family6LookupCount + 1U;
    g_family6Lookups[g_family6LookupCount++] = {caller, objectId, result};
    ReleaseSRWLockExclusive(&g_family6LookupLock);

    const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto callerRva = image != 0 && caller >= image
                               ? static_cast<unsigned long long>(caller - image)
                               : (std::numeric_limits<unsigned long long>::max)();
    std::array<char, 256> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=queuez_family6 stage=lookup n=%zu object=0x%08X result=%d caller=%p"
        " caller_rva=0x%llX mutation=observe_only",
        sequence,
        objectId,
        result,
        reinterpret_cast<void*>(caller),
        callerRva);
    if (count > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::info,
            {line.data(),
             (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
    }

    const std::uintptr_t window = caller >= kFamily6CallerBytesBefore
                                      ? caller - kFamily6CallerBytesBefore
                                      : caller;
    std::array<char, 64> subject{};
    (void)std::snprintf(subject.data(), subject.size(), "family6_lookup_caller_%zu", sequence);
    report_bytes<1280>(subject.data(),
                       6,
                       reinterpret_cast<const std::byte*>(window),
                       kFamily6CallerBytesBefore + kFamily6CallerBytesAfter);
}

bool is_resolved() noexcept {
    return g_resolved.load(std::memory_order_acquire);
}

} // namespace dawn::client::targets::game::content
