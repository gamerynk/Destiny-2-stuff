#include "handle_resolver.h"

#include <limits>

#include "layout.h"

namespace dawn::client::content::handles {
namespace {

constexpr std::uint32_t kRecordIndexBits = 13;
constexpr std::uint32_t kRecordIndexMask = (1U << kRecordIndexBits) - 1U;
constexpr std::uint32_t kHandleSignBit = 1U << ((std::numeric_limits<std::uint32_t>::digits) - 1U);
constexpr std::uint32_t kSignedHighFill =
    ~((std::numeric_limits<std::uint32_t>::max)() >> kRecordIndexBits);
constexpr std::uint64_t kTableMaskFill = 0x0FFC0000ULL;
constexpr unsigned kTableMaskShift = 18;
constexpr unsigned kHighBitTableIndexBits = 14;
constexpr std::uint64_t kMinimumContentTableIndex = 1024;
constexpr std::uint64_t kMaximumTableIndex = (1ULL << kHighBitTableIndexBits) - 1ULL;
constexpr std::uint32_t kUnavailableContentHandle = (std::numeric_limits<std::uint32_t>::max)();

[[nodiscard]] bool
add_fits(std::uintptr_t left, std::uintptr_t right, std::uintptr_t& result) noexcept {
    if (right > (std::numeric_limits<std::uintptr_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool
multiply_fits(std::uintptr_t left, std::uintptr_t right, std::uintptr_t& result) noexcept {
    if (left != 0 && right > (std::numeric_limits<std::uintptr_t>::max)() / left) {
        return false;
    }
    result = left * right;
    return true;
}

template <typename Value>
[[nodiscard]] bool read_value(const Source& source, std::uintptr_t address, Value& value) noexcept {
    return source.read != nullptr
           && source.read(source.context,
                          address,
                          std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

} // namespace

[[nodiscard]] bool resolve_from_table(const Source& source,
                                      std::uint32_t handle,
                                      std::uint64_t minimumTableIndex,
                                      std::uintptr_t& address) noexcept {
    address = 0;
    if (source.tablesSlot == 0 || source.read == nullptr || handle == kUnavailableContentHandle) {
        return false;
    }

    std::uintptr_t tablesObject = 0;
    std::uintptr_t tableBase = 0;
    if (!read_value(source, source.tablesSlot, tablesObject) || tablesObject == 0
        || !read_value(source, tablesObject, tableBase) || tableBase == 0) {
        return false;
    }

    // explicit sign extension keeps content table 1024 from aliasing generic table zero
    const std::uint32_t encodedHigh =
        (handle >> kRecordIndexBits) | (((handle & kHandleSignBit) != 0) ? kSignedHighFill : 0U);
    const std::uint64_t tableMask =
        (static_cast<std::uint64_t>(encodedHigh) | kTableMaskFill) >> kTableMaskShift;
    const std::uint64_t tableIndex = static_cast<std::uint16_t>(encodedHigh) & tableMask;
    if (tableIndex < minimumTableIndex || tableIndex > kMaximumTableIndex) {
        return false;
    }

    std::uintptr_t tableOffset = 0;
    std::uintptr_t tableAddress = 0;
    if (!multiply_fits(
            static_cast<std::uintptr_t>(tableIndex), sizeof(layout::TableDescriptor), tableOffset)
        || !add_fits(tableBase, tableOffset, tableAddress)) {
        return false;
    }

    layout::TableDescriptor table{};
    if (!read_value(source, tableAddress, table) || table.recordArray == 0
        || table.recordStride == 0) {
        return false;
    }

    const std::uint32_t recordIndex = handle & kRecordIndexMask;
    std::uintptr_t recordOffset = 0;
    std::uintptr_t recordAddress = 0;
    layout::RecordPrefix record{};
    if (!multiply_fits(recordIndex, table.recordStride, recordOffset)
        || recordOffset > (std::numeric_limits<std::uint32_t>::max)()
        || !add_fits(table.recordArray, recordOffset, recordAddress)
        || !read_value(source, recordAddress, record)) {
        return false;
    }

    const std::uint64_t extendedMask =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(table.correctionMask));
    const std::uint64_t correction = record.correctionSource & extendedMask;
    // retail subtracts in a 64-bit register and some schema tables hold a negative correction,
    // so rejecting correction > recordAddress would drop valid definitions
    address = static_cast<std::uintptr_t>(static_cast<std::uint64_t>(recordAddress) - correction);
    return address != 0;
}

bool resolve(const Source& source, std::uint32_t handle, std::uintptr_t& address) noexcept {
    return resolve_from_table(source, handle, kMinimumContentTableIndex, address);
}

bool resolve_schema(const Source& source,
                    std::uint32_t handle,
                    std::uintptr_t& address) noexcept {
    return resolve_from_table(source, handle, 0, address);
}

} // namespace dawn::client::content::handles
