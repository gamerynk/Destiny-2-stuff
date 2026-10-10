#include "status_fields.h"

#include <limits>

namespace dawn::middleware::web_service::status {
namespace {

// 5-bit status fields store logical success 0 as 1
constexpr std::uint8_t kStatusWidth = 5;
constexpr std::int32_t kStatusBias = 1;
constexpr std::uint32_t kStatusMask = (1U << kStatusWidth) - 1U;
constexpr std::uint8_t kStatusValueWidth = 32;
constexpr std::uint8_t kRequiredBoolWidth = 1;

[[nodiscard]] bool write_code(encoding::bits::Writer& writer, std::int32_t code) noexcept {
    const auto biased =
        static_cast<std::uint32_t>(static_cast<std::int64_t>(code) + kStatusBias) & kStatusMask;
    return writer.write(biased, kStatusWidth);
}

[[nodiscard]] bool write_value(encoding::bits::Writer& writer, std::int32_t value) noexcept {
    const std::int64_t biased =
        static_cast<std::int64_t>(value) - (std::numeric_limits<std::int32_t>::min)();
    return writer.write(static_cast<std::uint32_t>(biased), kStatusValueWidth);
}

} // namespace

bool write_fields(encoding::bits::Writer& writer,
                  ResponseShape shape,
                  const StatusResponse& response) noexcept {
    if (shape == ResponseShape::generic) {
        // generic replies carry no status field; only the envelope trailer follows
        return true;
    }
    bool encoded = write_code(writer, response.code);
    if (shape != ResponseShape::statusOnly) {
        encoded = encoded && write_value(writer, response.value);
    }
    if (shape == ResponseShape::statusPairWithBool) {
        encoded = encoded && writer.write(response.trailingBool ? 1U : 0U, kRequiredBoolWidth);
    }
    return encoded;
}

} // namespace dawn::middleware::web_service::status
