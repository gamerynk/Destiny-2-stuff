#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/roster_intersection.h"
#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "../../../middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../../hooks/retail_log/retail_log_enqueue_observer.h"
#include "internal.h"
#include "scenario_roster_host_slots.h"

namespace dawn::client::content::scenarios {
namespace {

namespace tables = middleware::content::packages::tables;

constexpr std::size_t kDescriptorTraversalCapacity = 512;
constexpr std::uint32_t kOmegaOpeningObject = 0x80F47BB2;
// Omega slot families whose client descriptor presence is not yet proven
constexpr std::uint16_t kOmegaUnknownSlotA = 48;
constexpr std::uint16_t kOmegaUnknownSlotB = 60;

void report_omega_opening_slots(const layouts::RosterGroup& group) noexcept {
    for (std::size_t slot = 0; slot < group.slotCount; ++slot) {
        const std::uint8_t type = group.slotTypes[slot];
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=scenario stage=omega_opening_slot key=0x%08X "
                                          "object=0x%08X index=%u type=%u flags=%u",
                                          group.registryKey,
                                          group.objectTag,
                                          static_cast<unsigned>(group.slotIndices[slot]),
                                          static_cast<unsigned>(type),
                                          static_cast<unsigned>(group.slotFlags[slot]));
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

bool collect_slot(void* context, const tables::SlotDescriptor& descriptor) noexcept {
    auto& storage = *static_cast<RosterStorage*>(context);
    if (descriptor.slotType == 0 || descriptor.slotType > layouts::kMaximumSlotType
        || descriptor.slotIndex >= layouts::kRosterSlotCapacity) {
        return true;
    }
    bool firstOfType = true;
    for (std::size_t slot = 0; slot < storage.slotCount; ++slot) {
        if (storage.slots[slot].index == descriptor.slotIndex) {
            return true;
        }
        firstOfType = firstOfType && storage.slots[slot].type != descriptor.slotType;
    }
    const bool diagnosticType = descriptor.slotType == 13 || descriptor.slotType == 16
                                || descriptor.slotType == 17 || descriptor.slotType == 18
                                || descriptor.slotType == 35 || descriptor.slotType == 41
                                || descriptor.slotType == kOmegaUnknownSlotA
                                || descriptor.slotType == kOmegaUnknownSlotB;
    if (diagnosticType && firstOfType) {
        client::hooks::retail_log::register_schema_marker(descriptor.componentClass);
        client::hooks::retail_log::register_schema_marker(descriptor.senseSchema);
        client::hooks::retail_log::register_schema_marker(descriptor.authSchema);
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=scenario stage=slot_schema type=%u component=0x%08X "
                                          "sense=0x%08X auth=0x%08X",
                                          static_cast<unsigned>(descriptor.slotType),
                                          descriptor.componentClass,
                                          descriptor.senseSchema,
                                          descriptor.authSchema);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    if (storage.slotCount == storage.slots.size()) {
        storage.slotsOverflowed = true;
        return true;
    }
    std::uint8_t flags = 0;
    if (descriptor.authSchema != tables::kAbsentSchema) {
        flags |= layouts::kSlotAuthFlag;
    }
    if (descriptor.senseSchema != tables::kAbsentSchema) {
        flags |= layouts::kSlotSenseFlag;
    }
    storage.slots[storage.slotCount++] = {
        descriptor.slotIndex, static_cast<std::uint8_t>(descriptor.slotType), flags};
    return true;
}

void follow_handle(const reader::Source& source,
                   reader::Scratch& scratch,
                   RosterStorage& storage,
                   std::uint32_t handle,
                   std::uint32_t registryKey) noexcept {
    std::array<std::uint32_t, kDescriptorTraversalCapacity> pending{};
    pending[0] = handle;
    std::size_t cursor = 0;
    std::size_t pendingCount = 1;
    while (cursor < pendingCount && cursor < kDescriptorTraversalCapacity) {
        const std::uint32_t tag = pending[cursor++];
        std::uint32_t classId = 0;
        ++storage.reads;
        if (!reader::read_tag(source, scratch, tag, storage.chain, classId)) {
            continue;
        }
        if (classId == tables::kPlacedObjectClass) {
            (void)tables::visit_slot_descriptors(
                storage.chain, tag, registryKey, &collect_slot, &storage);
            continue;
        }
        std::size_t childCount = 0;
        const std::span<std::uint32_t> remaining(pending.data() + pendingCount,
                                                 pending.size() - pendingCount);
        if (!tables::descriptor_child_tags(storage.chain, classId, remaining, childCount)) {
            continue;
        }
        pendingCount += childCount;
    }
}

void collect_descriptors(const reader::Source& source,
                         reader::Scratch& scratch,
                         RosterStorage& storage,
                         std::span<const std::byte> objectBlob,
                         std::uint32_t registryKey) noexcept {
    tables::Array bubbles{};
    if (!tables::object_bubbles(objectBlob, bubbles)) {
        return;
    }
    for (std::uint64_t index = 0; index < bubbles.count; ++index) {
        tables::ObjectBubble bubble{};
        if (!tables::object_bubble_at(objectBlob, bubbles, index, bubble)) {
            continue;
        }
        for (std::uint64_t slot = 0; slot < bubble.handleCount; ++slot) {
            std::uint32_t handle = 0;
            if (!tables::object_placed_handle_at(objectBlob, bubble, slot, handle)) {
                continue;
            }
            follow_handle(source, scratch, storage, handle, registryKey);
        }
    }
}

void add_host_slots(RosterStorage& storage,
                    std::span<const std::byte> objectBlob,
                    const tables::Array& declared,
                    std::uint32_t objectTag,
                    std::uint32_t registryKey) noexcept {
    for (std::uint64_t index = 0; index < declared.count; ++index) {
        tables::Slot slot{};
        if (!tables::object_slot_at(objectBlob, declared, index, slot)) {
            continue;
        }
        const bool omega = objectTag == kOmegaOpeningObject
                           && (slot.type == kOmegaUnknownSlotA
                               || slot.type == kOmegaUnknownSlotB);
        if (!omega && !host_slots::crucible_host_slot(registryKey, slot.type)) {
            continue;
        }
        const auto existing = std::find_if(storage.slots.begin(),
                                           storage.slots.begin() + storage.slotCount,
                                           [index](const SlotRecord& candidate) {
                                               return candidate.index == index;
                                           });
        if (existing != storage.slots.begin() + storage.slotCount) {
            continue;
        }
        if (storage.slotCount == storage.slots.size()) {
            storage.slotsOverflowed = true;
            return;
        }
        storage.slots[storage.slotCount++] = {
            static_cast<std::uint16_t>(index), static_cast<std::uint8_t>(slot.type), 0};
    }
}

// a slot with no client descriptor is host-owned: pvp_shaft's per-team spawns declare both
// sense and auth schema absent, and requiring a descriptor for every slot would discard the object
void add_host_only_slots(RosterStorage& storage,
                         std::span<const std::byte> objectBlob,
                         const tables::Array& declared) noexcept {
    for (std::uint64_t index = 0; index < declared.count; ++index) {
        tables::Slot slot{};
        if (!tables::object_slot_at(objectBlob, declared, index, slot)) {
            continue;
        }
        const auto last = storage.slots.begin() + static_cast<std::ptrdiff_t>(storage.slotCount);
        const auto existing =
            std::find_if(storage.slots.begin(), last, [index](const SlotRecord& candidate) {
                return candidate.index == index;
            });
        if (existing != last) {
            continue;
        }
        if (storage.slotCount == storage.slots.size()) {
            storage.slotsOverflowed = true;
            return;
        }
        storage.slots[storage.slotCount++] = {
            static_cast<std::uint16_t>(index), static_cast<std::uint8_t>(slot.type), 0};
    }
}

constexpr std::size_t kHostOnlyReportBudget = 24;
constexpr std::uint64_t kAlwaysReportSlotCount = 100;

std::size_t g_hostOnlyReports = 0;

void report_host_only_fill(std::uint32_t objectTag,
                           std::uint64_t declaredCount,
                           std::size_t describedCount,
                           std::size_t filledCount) noexcept {
    if (g_hostOnlyReports >= kHostOnlyReportBudget && declaredCount < kAlwaysReportSlotCount) {
        return;
    }
    ++g_hostOnlyReports;
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=scenario stage=host_only object=0x%08X declared=%llu "
                                      "described=%zu filled=%zu added=%zu",
                                      objectTag,
                                      static_cast<unsigned long long>(declaredCount),
                                      describedCount,
                                      filledCount,
                                      filledCount - describedCount);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool fill_slots(RosterStorage& storage,
                              std::size_t declaredSlotCount,
                              layouts::RosterGroup& group) noexcept {
    if (storage.slotsOverflowed || storage.slotCount == 0
        || storage.slotCount != declaredSlotCount) {
        return false;
    }
    const auto last = storage.slots.begin() + static_cast<std::ptrdiff_t>(storage.slotCount);
    std::sort(storage.slots.begin(), last, [](const SlotRecord& left, const SlotRecord& right) {
        return left.index < right.index;
    });
    for (std::size_t slot = 0; slot < storage.slotCount; ++slot) {
        group.slotIndices[slot] = storage.slots[slot].index;
        group.slotTypes[slot] = storage.slots[slot].type;
        group.slotFlags[slot] = storage.slots[slot].flags;
    }
    group.slotCount = static_cast<std::uint16_t>(storage.slotCount);
    return true;
}

[[nodiscard]] std::size_t memo_slot(const RosterStorage& storage, std::uint32_t tag) noexcept {
    std::size_t probe = tag % kObjectMemoCapacity;
    for (std::size_t step = 0; step < kObjectMemoCapacity; ++step) {
        if (storage.memo[probe].tag == 0 || storage.memo[probe].tag == tag) {
            return probe;
        }
        probe = (probe + 1) % kObjectMemoCapacity;
    }
    return kObjectMemoCapacity;
}

} // namespace

bool resolve_object(const reader::Source& source,
                    reader::Scratch& scratch,
                    RosterStorage& storage,
                    std::uint32_t objectTag,
                    std::uint16_t& group) noexcept {
    group = kNotARosterGroup;
    const std::size_t slot = memo_slot(storage, objectTag);
    if (slot == kObjectMemoCapacity) {
        return false;
    }
    if (storage.memo[slot].tag == objectTag) {
        group = storage.memo[slot].group;
        return true;
    }
    storage.memo[slot].tag = objectTag;
    storage.memo[slot].group = kNotARosterGroup;
    ++storage.reads;
    const bool objectRead = reader::read_tag(source, scratch, objectTag, storage.object);
    if (objectTag == kOmegaOpeningObject && !objectRead) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=scenario stage=omega_opening_object result=read_failed");
    }
    if (!objectRead) {
        return true;
    }
    const bool rosterCandidate = tables::carries_roster_slot(storage.object, objectTag);
    if (!rosterCandidate) {
        return true;
    }

    layouts::RosterGroup candidate{};
    tables::Array declared{};
    if (!tables::object_key(storage.object, candidate.registryKey) || candidate.registryKey == 0
        || !tables::object_slots(storage.object, declared) || declared.count == 0
        || declared.count > layouts::kRosterSlotCapacity) {
        if (objectTag == kOmegaOpeningObject) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::warn,
                             "ev=scenario stage=omega_opening_object result=invalid_slots");
        }
        return true;
    }
    storage.slotCount = 0;
    storage.slotsOverflowed = false;
    collect_descriptors(source, scratch, storage, storage.object, candidate.registryKey);
    if (objectTag == kOmegaOpeningObject || host_slots::crucible_group(candidate.registryKey)) {
        // keep Omega's host-only 48/60 handling; Crucible also has host-only point/volume/script slots,
        // and rejecting the group for them discarded the real type-50 spawn components (class 0x808094D1)
        add_host_slots(storage, storage.object, declared, objectTag, candidate.registryKey);
    }
    if (!fill_slots(storage, declared.count, candidate)) {
        // a descriptor-short group cost pvp_shaft everything: host served 19 of 317 sensor slots.
        // Name the rest host-only; only short groups reach this, so working ones keep their shape
        const std::size_t described = storage.slotCount;
        add_host_only_slots(storage, storage.object, declared);
        report_host_only_fill(objectTag, declared.count, described, storage.slotCount);
        if (!fill_slots(storage, declared.count, candidate)) {
            ++storage.unresolvedGroups;
            return true;
        }
    }
    candidate.objectTag = objectTag;
    if (objectTag == kOmegaOpeningObject) {
        report_omega_opening_slots(candidate);
    }
    // one key may carry different layouts in different activities, so only exact layouts reuse
    for (std::size_t index = 0; index < storage.groupCount; ++index) {
        if (same_group_layout(storage.groups[index], candidate)) {
            storage.memo[slot].group = static_cast<std::uint16_t>(index);
            group = storage.memo[slot].group;
            return true;
        }
    }
    if (storage.groupCount == layouts::kRosterGroupCapacity) {
        return false;
    }
    storage.groups[storage.groupCount] = candidate;
    storage.memo[slot].group = static_cast<std::uint16_t>(storage.groupCount);
    group = storage.memo[slot].group;
    ++storage.groupCount;
    return true;
}

} // namespace dawn::client::content::scenarios
