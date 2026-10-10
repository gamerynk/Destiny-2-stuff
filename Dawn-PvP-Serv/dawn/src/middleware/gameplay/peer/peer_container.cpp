#include "peer_container.h"

namespace dawn::middleware::gameplay::peer {

namespace {

constexpr std::uint8_t kMarkerWidth = 1;
constexpr std::uint64_t kContainerMarker = 1;
constexpr std::uint8_t kFollowsWidth = 1;
constexpr std::uint8_t kIdWidth = 6;
constexpr std::uint8_t kSizeWidth = 18;

} // namespace

bool open_container(encoding::bits::Writer& writer) noexcept {
    return writer.write(kContainerMarker, kMarkerWidth);
}

bool write_header(encoding::bits::Writer& writer, const MessageHeader& header) noexcept {
    return header.id <= kMaximumMessageId && writer.write(1, kFollowsWidth)
           && writer.write(header.id, kIdWidth) && writer.write(header.declaredSize, kSizeWidth);
}

bool close_container(encoding::bits::Writer& writer) noexcept {
    return writer.write(0, kFollowsWidth);
}

bool read_marker(encoding::bits::Reader& reader) noexcept {
    std::uint64_t marker = 0;
    return reader.read(kMarkerWidth, marker) && marker == kContainerMarker;
}

bool read_header(encoding::bits::Reader& reader, MessageHeader& header, bool& present) noexcept {
    present = false;
    std::uint64_t follows = 0;
    if (!reader.read(kFollowsWidth, follows)) {
        return false;
    }
    if (follows == 0) {
        return true;
    }
    std::uint64_t id = 0;
    std::uint64_t size = 0;
    if (!reader.read(kIdWidth, id) || id > kMaximumMessageId || !reader.read(kSizeWidth, size)) {
        return false;
    }
    header.id = static_cast<std::uint8_t>(id);
    header.declaredSize = static_cast<std::uint32_t>(size);
    present = true;
    return true;
}

} // namespace dawn::middleware::gameplay::peer
