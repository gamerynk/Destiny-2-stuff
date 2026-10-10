#include "snapshot_storage.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <span>

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

[[nodiscard]] bool contained_end(std::span<const std::byte> owner,
                                 std::span<const std::byte> payload,
                                 std::size_t& endOffset) noexcept {
    endOffset = 0;
    if (payload.empty()) {
        return true;
    }
    const auto ownerAddress = reinterpret_cast<std::uintptr_t>(owner.data());
    const auto payloadAddress = reinterpret_cast<std::uintptr_t>(payload.data());
    if (payloadAddress < ownerAddress) {
        return false;
    }
    const std::uintptr_t offset = payloadAddress - ownerAddress;
    if (offset > owner.size()) {
        return false;
    }
    const auto narrowedOffset = static_cast<std::size_t>(offset);
    if (payload.size() > owner.size() - narrowedOffset) {
        return false;
    }
    endOffset = narrowedOffset + payload.size();
    return true;
}

} // namespace

Reservation reserve_prior(const Scratch& scratch, const Prepared& prepared) noexcept {
    const std::size_t objectCount = prepared.family.objects.size();
    if (prepared.rawClearSize > scratch.plaintext.size()
        || prepared.compressedClearSize > scratch.sealed.size()
        || objectCount > prepared.objects.size()
        || (objectCount != 0 && prepared.family.objects.data() != prepared.objects.data())) {
        return {};
    }

    Reservation reservation{
        0,
        0,
        prepared.rawClearSize,
        prepared.compressedClearSize,
    };
    const std::span<const std::byte> rawStorage{scratch.plaintext};
    const std::span<const std::byte> compressedStorage{scratch.sealed};
    for (const middleware::queuez::Object& object : prepared.family.objects) {
        if (object.payload.empty()) {
            continue;
        }
        std::size_t payloadEnd = 0;
        // reserve the tail after each borrowed payload so staging can't overwrite prior output
        if (contained_end(rawStorage, object.payload, payloadEnd)) {
            reservation.rawWriteOffset = (std::max)(reservation.rawWriteOffset, payloadEnd);
        } else if (contained_end(compressedStorage, object.payload, payloadEnd)) {
            reservation.compressedWriteOffset =
                (std::max)(reservation.compressedWriteOffset, payloadEnd);
        } else {
            return {};
        }
    }
    if (reservation.rawWriteOffset > reservation.rawClearSize
        || reservation.compressedWriteOffset > reservation.compressedClearSize) {
        return {};
    }
    return reservation;
}

void clear_after(Scratch& scratch, const Reservation& reservation) noexcept {
    if (reservation.rawWriteOffset < scratch.plaintext.size()) {
        SecureZeroMemory(scratch.plaintext.data() + reservation.rawWriteOffset,
                         scratch.plaintext.size() - reservation.rawWriteOffset);
    }
    if (reservation.compressedWriteOffset < scratch.sealed.size()) {
        SecureZeroMemory(scratch.sealed.data() + reservation.compressedWriteOffset,
                         scratch.sealed.size() - reservation.compressedWriteOffset);
    }
}

bool commit(const Prepared& staged, Prepared& output) noexcept {
    const std::size_t objectCount = staged.family.objects.size();
    if (objectCount > staged.objects.size()
        || (objectCount != 0 && staged.family.objects.data() != staged.objects.data())) {
        return false;
    }

    output.objects = staged.objects;
    output.rawClearSize = staged.rawClearSize;
    output.compressedClearSize = staged.compressedClearSize;
    output.family = middleware::queuez::Family{
        staged.family.type,
        staged.family.rootSoid,
        staged.family.version,
        staged.family.flags,
        std::span(output.objects).first(objectCount),
    };
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
