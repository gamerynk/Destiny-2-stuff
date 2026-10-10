#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include "layout.h"

namespace dawn::middleware::datagen::family4::character {
struct AcquisitionNotice { std::uint64_t characterSoid{}, instanceSoid{}; };
// call only after the complete frame was accepted into the transport output
inline void commit_acquisition_batch(std::span<AcquisitionNotice> pending,
    std::size_t& count, std::size_t batch) noexcept {
    if (batch>count || count>pending.size()) return;
    std::move(pending.begin()+batch,pending.begin()+count,pending.begin());
    count-=batch;
    std::fill(pending.begin()+count,pending.end(),AcquisitionNotice{});
}
inline void apply_acquisition_feedback(layout::Object& object,
    std::span<const AcquisitionNotice> notices) noexcept {
    object.inventoryChanges = {};
    std::size_t count=0;
    for (const auto& notice : notices) {
        if (notice.characterSoid != object.characterSoid) continue;
        const auto row=std::find_if(object.inventoryItems.begin(),object.inventoryItems.end(),
            [&](const auto& value) { return value.instanceSoid == notice.instanceSoid; });
        if (row==object.inventoryItems.end() || row->mutationSerial<0
            || count>=object.inventoryChanges.records.size()) continue;
        auto& record=object.inventoryChanges.records[count];
        record.sequence=static_cast<std::uint16_t>(count);
        record.mutationSerial=row->mutationSerial; record.kind=1; record.flags=0;
        ++count;
    }
    object.inventoryChanges.writeSlot=static_cast<std::uint16_t>(count % object.inventoryChanges.records.size());
    object.inventoryChanges.nextSequence=static_cast<std::uint16_t>(count);
}
}
