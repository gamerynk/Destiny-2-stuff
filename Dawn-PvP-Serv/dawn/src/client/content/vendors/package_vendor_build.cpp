#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/definition_index_table.h"
#include "../../../state/build_data/runtime.h"
#include "../../../state/build_data/vendors/definition.h"
#include "layout.h"
#include "vendor_build.h"

namespace dawn::client::content::vendors {
namespace {

namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;
namespace domain = state::build_data::vendors;

struct Storage {
    std::vector<std::byte> blob{};
    std::array<domain::IndexEntry, domain::kIndexCapacity> index{};
    std::array<domain::Definition, domain::kDefinitionCapacity> definitions{};
    std::array<domain::SaleRow, domain::kSaleRowCapacity> saleRows{};
    std::array<domain::InstalledRow, domain::kInstalledRowCapacity> installedRows{};
    std::size_t indexCount{};
    std::size_t definitionCount{};
    std::size_t saleRowCount{};
    std::size_t installedRowCount{};
};

struct ArrayView {
    std::uint32_t base{};
    std::uint32_t classId{};
    std::uint16_t count{};
};

template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

[[nodiscard]] bool read_array(std::span<const std::byte> blob,
                              std::size_t descriptor,
                              std::size_t stride,
                              ArrayView& output) noexcept {
    constexpr std::uint64_t kMaximumCount = (std::numeric_limits<std::uint16_t>::max)();
    output = {};
    std::uint64_t declared = 0;
    if (!read(blob, descriptor, declared)) {
        return false;
    }
    if (declared == 0) {
        return true;
    }
    tables::Array array{};
    if (!tables::find_array_at(blob, descriptor, array) || array.count > kMaximumCount) {
        return false;
    }
    const std::uint64_t end = array.dataOffset + (array.count * stride);
    if (end > blob.size()) {
        return false;
    }
    output = {static_cast<std::uint32_t>(array.dataOffset),
              array.elementClass,
              static_cast<std::uint16_t>(array.count)};
    return true;
}

[[nodiscard]] bool
read_count(std::span<const std::byte> blob, std::size_t descriptor, std::uint32_t& count) noexcept {
    constexpr std::uint64_t kMaximumCount = (std::numeric_limits<std::uint32_t>::max)();
    count = 0;
    std::uint64_t declared = 0;
    if (!read(blob, descriptor, declared) || declared > kMaximumCount) {
        return false;
    }
    count = static_cast<std::uint32_t>(declared);
    return true;
}

[[nodiscard]] bool
read_index(const reader::Source& source, reader::Scratch& scratch, Storage& storage) noexcept {
    std::uint32_t classId = 0;
    tables::Array array{};
    if (!reader::read_tag(source, scratch, kIndexRootTag, storage.blob, classId)
        || classId != domain::kIndexWrapperClass) {
        return false;
    }
    const std::span<const std::byte> blob{storage.blob};
    if (!tables::find_array_at(blob, tables::kTableArrayDescriptor, array)
        || array.elementClass != domain::kIndexRowClass || array.count > domain::kIndexCapacity) {
        return false;
    }
    for (std::uint64_t row = 0; row < array.count; ++row) {
        tables::IndexRow entry{};
        if (!tables::index_row(blob, array, row, entry)) {
            return false;
        }
        storage.index[storage.indexCount] = {
            entry.definitionHash, entry.targetTag, static_cast<std::uint16_t>(row)};
        ++storage.indexCount;
    }
    return storage.indexCount != 0;
}

[[nodiscard]] bool read_sale_rows(std::span<const std::byte> blob,
                                  const domain::Definition& definition,
                                  Storage& storage) noexcept {
    if (definition.saleCount > domain::kSaleRowCapacity - storage.saleRowCount) {
        return false;
    }
    for (std::size_t row = 0; row < definition.saleCount; ++row) {
        const std::size_t at = definition.saleRowBase + (row * domain::kSaleRowStride);
        domain::SaleRow& value = storage.saleRows[storage.saleRowCount + row];
        value = {};
        value.vendorIndex = definition.index;
        value.rowIndex = static_cast<std::uint16_t>(row);
        if (!read(blob, at + kSaleItemIndexOffset, value.itemIndex)
            || !read(blob, at + kSaleSecondaryItemOffset, value.secondaryItemIndex)
            || !read(blob, at + kSaleCategoryIndexOffset, value.categoryIndex)
            || !read(blob, at + kSaleRaw104Offset, value.raw104)
            || !read(blob, at + kSaleRaw108Offset, value.raw108)
            || !read(blob, at + kSaleRaw172Offset, value.raw172)
            || !read(blob, at + kSaleFeatureBranchOffset, value.featureBranch)
            || !read_count(blob, at + kSaleExpression8Offset, value.expressionCount8)
            || !read_count(blob, at + kSaleNestedRecordOffset, value.nestedRecordCount)
            || !read_count(blob, at + kSaleExpression120Offset, value.expressionCount120)
            || !read_count(blob, at + kSaleCount136Offset, value.count136)
            || !read_count(blob, at + kSaleExpression160Offset, value.expressionCount160)) {
            return false;
        }
    }
    storage.saleRowCount += definition.saleCount;
    return true;
}

[[nodiscard]] bool read_installed_rows(std::span<const std::byte> blob,
                                       const domain::Definition& definition,
                                       Storage& storage) noexcept {
    if (definition.installedCount > domain::kInstalledRowCapacity - storage.installedRowCount) {
        return false;
    }
    for (std::size_t row = 0; row < definition.installedCount; ++row) {
        const std::size_t at = definition.installedRowBase + (row * domain::kInstalledRowStride);
        domain::InstalledRow& value = storage.installedRows[storage.installedRowCount + row];
        value = {};
        value.vendorIndex = definition.index;
        value.rowIndex = static_cast<std::uint16_t>(row);
        if (!read(blob, at, value.raw)) {
            return false;
        }
    }
    storage.installedRowCount += definition.installedCount;
    return true;
}

[[nodiscard]] bool read_definition(const reader::Source& source,
                                   reader::Scratch& scratch,
                                   const domain::IndexEntry& entry,
                                   Storage& storage) noexcept {
    constexpr std::size_t kMaximumSize = (std::numeric_limits<std::uint32_t>::max)();
    std::uint32_t classId = 0;
    if (storage.definitionCount == domain::kDefinitionCapacity
        || !reader::read_tag(source, scratch, entry.definitionTag, storage.blob, classId)
        || classId != domain::kDefinitionClass || storage.blob.size() > kMaximumSize) {
        return false;
    }
    const std::span<const std::byte> blob{storage.blob};
    ArrayView installed{};
    ArrayView sale{};
    ArrayView third{};
    if (!read_array(blob, kInstalledArrayDescriptor, domain::kInstalledRowStride, installed)
        || !read_array(blob, kSaleArrayDescriptor, domain::kSaleRowStride, sale)
        || !read_array(blob, kThirdArrayDescriptor, domain::kThirdRowStride, third)) {
        return false;
    }
    domain::Definition definition{};
    definition.definitionHash = entry.definitionHash;
    definition.definitionTag = entry.definitionTag;
    definition.definitionClass = classId;
    definition.definitionSize = static_cast<std::uint32_t>(blob.size());
    definition.index = entry.index;
    definition.installedRowBase = installed.base;
    definition.installedRowClass = installed.classId;
    definition.installedCount = installed.count;
    definition.saleRowBase = sale.base;
    definition.saleRowClass = sale.classId;
    definition.saleCount = sale.count;
    definition.thirdRowBase = third.base;
    definition.thirdRowClass = third.classId;
    definition.thirdCount = third.count;
    definition.saleRowOffset = static_cast<std::uint32_t>(storage.saleRowCount);
    definition.installedRowOffset = static_cast<std::uint32_t>(storage.installedRowCount);
    // each row reader advances its bank before the other runs, so a half-fitting definition
    // leaves orphans and valid() rejects the set. A skipped one leaves both banks as found
    const std::size_t saleRowsBefore = storage.saleRowCount;
    const std::size_t installedRowsBefore = storage.installedRowCount;
    if (!read(blob, kResetIntervalOffset, definition.resetIntervalRaw)
        || !read(blob, kResetPhaseOffset, definition.resetPhaseRaw)
        || !read_sale_rows(blob, definition, storage)
        || !read_installed_rows(blob, definition, storage)) {
        storage.saleRowCount = saleRowsBefore;
        storage.installedRowCount = installedRowsBefore;
        return false;
    }
    storage.definitions[storage.definitionCount] = definition;
    ++storage.definitionCount;
    return true;
}

// named hashes come first, each checked against the index: a hash it lacks is a mistyped rule,
// and dropping it silently looks like the vendor resolving. A duplicate would waste a slot
[[nodiscard]] std::size_t select_definitions(const Storage& storage,
                                             std::span<const std::uint32_t> namedHashes,
                                             std::span<std::uint32_t> hashes) noexcept {
    std::size_t wanted = 0;
    for (std::size_t at = 0; at < namedHashes.size() && wanted < hashes.size(); ++at) {
        bool present = false;
        for (std::size_t held = 0; held < wanted && !present; ++held) {
            present = hashes[held] == namedHashes[at];
        }
        if (present) {
            continue;
        }
        bool installed = false;
        for (std::size_t row = 0; row < storage.indexCount && !installed; ++row) {
            installed = storage.index[row].definitionHash == namedHashes[at];
        }
        if (installed) {
            hashes[wanted++] = namedHashes[at];
            continue;
        }
        core::log::writef(core::log::Channel::state,
                          core::log::Level::warn,
                          "ev=vendor stage=catalog result=skip reason=unknown_hash hash=0x%08X",
                          namedHashes[at]);
    }
    for (std::size_t row = 0; row < storage.indexCount && wanted < hashes.size(); ++row) {
        bool present = false;
        for (std::size_t at = 0; at < wanted && !present; ++at) {
            present = hashes[at] == storage.index[row].definitionHash;
        }
        if (!present) {
            hashes[wanted++] = storage.index[row].definitionHash;
        }
    }
    return wanted;
}

[[nodiscard]] bool requested(std::span<const std::uint32_t> hashes, std::uint32_t hash) noexcept {
    for (const std::uint32_t value : hashes) {
        if (value == hash) {
            return true;
        }
    }
    return false;
}

void report(const Storage& storage, std::size_t skipped, const char* result) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=build_data stage=vendors index=%zu definitions=%zu "
                                      "sale=%zu installed=%zu skipped=%zu result=%s",
                                      storage.indexCount,
                                      storage.definitionCount,
                                      storage.saleRowCount,
                                      storage.installedRowCount,
                                      skipped,
                                      result);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         storage.indexCount != 0 && skipped == 0 ? core::log::Level::info
                                                                 : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

bool build(const reader::Source& source,
           reader::Scratch& scratch,
           std::span<const std::uint32_t> namedHashes) noexcept {
    if (state::build_data::vendor_catalog_ready()) {
        return true;
    }
    static Storage storage{};
    storage = {};
    if (!read_index(source, scratch, storage)) {
        report(storage, 0, "index");
        return false;
    }
    static std::array<std::uint32_t, domain::kDefinitionCapacity> chosen{};
    const std::span<const std::uint32_t> definitionHashes =
        std::span(chosen).first(select_definitions(storage, namedHashes, chosen));
    // an unreadable or non-fitting definition costs that vendor only, not the pass; failing whole
    // once cached an empty catalog that left every vendor unresolvable on later boots
    std::size_t skipped = 0;
    for (std::size_t row = 0; row < storage.indexCount; ++row) {
        const domain::IndexEntry entry = storage.index[row];
        if (!requested(definitionHashes, entry.definitionHash)) {
            continue;
        }
        if (read_definition(source, scratch, entry, storage)) {
            continue;
        }
        ++skipped;
        core::log::writef(core::log::Channel::state,
                          core::log::Level::warn,
                          "ev=build_data stage=vendors result=skip hash=0x%08X row=%zu "
                          "definitions=%zu sale=%zu",
                          entry.definitionHash,
                          row,
                          storage.definitionCount,
                          storage.saleRowCount);
    }
    const bool published = state::build_data::publish_vendor_catalog(
        std::span(storage.index).first(storage.indexCount),
        std::span(storage.definitions).first(storage.definitionCount),
        std::span(storage.saleRows).first(storage.saleRowCount),
        std::span(storage.installedRows).first(storage.installedRowCount));
    report(storage, skipped, published ? "ok" : "publish");
    core::log::writef(core::log::Channel::state,
                      published ? core::log::Level::info : core::log::Level::warn,
                      "ev=vendor stage=catalog result=%s named=%zu requested=%zu index_rows=%zu",
                      published ? "ok" : "fail",
                      namedHashes.size(),
                      definitionHashes.size(),
                      storage.indexCount);
    return published;
}

} // namespace dawn::client::content::vendors
