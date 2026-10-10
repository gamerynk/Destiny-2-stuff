#include <cassert>
#include <string>
#include <vector>
#include "../dawn/src/steam/runtime/transport/native_certificate_publish.cpp"

namespace api = dawn::steam::runtime::transport::native_certificate_publish;
namespace fixture {
constexpr std::uint64_t platform = 0x01100001F0000003ULL;
dawn::core::settings::Settings settings{};
unsigned posts{}, proofs{};
unsigned status = 200;
bool identityMatches = true, resetDuringPost{}, oldServer{}, volatileServer{}, badAck{};
std::vector<std::string> events;
}
namespace dawn::core::settings {
const Settings& get() noexcept { return fixture::settings; }
}
namespace dawn::core::identity::device_identity {
std::uint64_t platform_id() noexcept { return fixture::identityMatches ? fixture::platform : fixture::platform + 1; }
bool issue(Scope scope, std::uint64_t now, std::span<std::byte, kProofSize> output) noexcept {
    assert(scope == Scope::transportCertificate && now != 0); ++fixture::proofs;
    std::fill(output.begin(), output.end(), std::byte{0xBA}); return true;
}
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view text) noexcept { fixture::events.emplace_back(text); }
}
namespace dawn::client::hooks::external_server {
bool consume(const client::network::HttpRequest& request, client::network::HttpResponse& response) noexcept {
    namespace wire = middleware::bap::native_certificate;
    assert(request.url == "/v1/transport/certificate" && request.method == client::network::HttpMethod::post);
    assert(request.body.size() > core::identity::device_identity::kProofSize);
    const auto proof = request.body.first(core::identity::device_identity::kProofSize);
    assert(std::all_of(proof.begin(), proof.end(), [](std::byte value) { return value == std::byte{0xBA}; }));
    wire::Metadata metadata{};
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    namespace publication = middleware::bap::native_certificate_publication;
    auto certificate = request.body.subspan(proof.size());
    const bool modern = publication::requested(certificate);
    if (modern) certificate = certificate.subspan(publication::kRequest.size());
    assert(wire::signed_metadata(certificate, fixture::platform, static_cast<std::uint64_t>(now), metadata));
    ++fixture::posts; response.statusCode = fixture::oldServer && modern ? 400 : fixture::status; response.size = 0;
    if (!fixture::oldServer && response.statusCode == 200) {
        const auto& ack = fixture::volatileServer ? publication::kVolatileAck : publication::kDurableAck;
        std::copy(ack.begin(), ack.end(), request.response.begin()); response.size = ack.size();
        if (fixture::badAck) request.response[3] = std::byte{2};
    }
    if (fixture::resetDuringPost) api::reset();
    return true;
}
}
std::array<std::byte, 792> real_shaped_callback(unsigned keyByte) {
    namespace proto = dawn::middleware::protobuf;
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::array<std::byte, 512> cert{}; proto::Writer writer(cert);
    std::array<std::byte, 32> key{}; key.fill(static_cast<std::byte>(keyByte));
    assert(writer.write_varint(1, 1) && writer.write_length_delimited(2, key));
    auto offset = writer.size();
    const auto fixed = [&](unsigned field, unsigned size, std::uint64_t value) {
        cert[offset++] = static_cast<std::byte>((field << 3) | (size == 8 ? 1 : 5));
        for (unsigned i = 0; i < size; ++i) cert[offset++] = static_cast<std::byte>(value >> (i * 8));
    };
    fixed(4, 8, fixture::platform); fixed(8, 4, now - 1); fixed(9, 4, now + 3600);
    proto::Writer app(std::span(cert).subspan(offset)); assert(app.write_varint(10, 1085660));
    offset += app.size();
    std::array<std::byte, 792> callback{};
    const std::uint32_t result = 1, size = static_cast<std::uint32_t>(offset), signature = 64;
    const std::uint64_t ca = 123;
    std::memcpy(callback.data(), &result, 4); std::memcpy(callback.data() + 4, &size, 4);
    std::copy_n(cert.begin(), offset, callback.begin() + 8);
    std::memcpy(callback.data() + 0x208, &ca, 8); std::memcpy(callback.data() + 0x210, &signature, 4);
    std::fill_n(callback.begin() + 0x214, 64, std::byte{0x42});
    return callback;
}
int main() {
    fixture::settings.client.externalServer.enabled = true;
    api::reset(); api::service(0); assert(fixture::posts == 0 && !api::published());
    api::capture(real_shaped_callback(3), fixture::platform); assert(!api::published());
    api::service(100); assert(fixture::posts == 1 && fixture::proofs == 1 && api::published() && api::durably_published());
    auto native = real_shaped_callback(3); std::uint32_t nativeSize{};
    std::memcpy(&nativeSize, native.data()+4, sizeof nativeSize);
    assert(api::durable_certificate_matches(std::span(native).subspan(8, nativeSize)));
    native = real_shaped_callback(4);
    assert(!api::durable_certificate_matches(std::span(native).subspan(8, nativeSize)));
    assert(!api::durable_certificate_matches({}));
    api::service(101); assert(fixture::posts == 1);
    api::service(30100); assert(fixture::posts == 2 && api::published());
    api::capture(real_shaped_callback(4), fixture::platform); assert(!api::published());
    fixture::status = 503; api::service(30101); assert(fixture::posts == 3 && !api::published());
    fixture::status = 200; api::service(30102); assert(fixture::posts == 3);
    api::service(35101); assert(fixture::posts == 4 && api::published());
    api::capture(real_shaped_callback(5), fixture::platform); fixture::resetDuringPost = true;
    api::service(35102); assert(fixture::posts == 5 && !api::published());
    fixture::resetDuringPost = false; fixture::identityMatches = false;
    api::capture(real_shaped_callback(6), fixture::platform); api::service(35103);
    assert(fixture::posts == 5 && !api::published());
    fixture::identityMatches = true;
    fixture::oldServer = true;
    api::capture(real_shaped_callback(7), fixture::platform); api::service(40000);
    assert(fixture::posts == 7 && api::published() && !api::durably_published());
    fixture::oldServer = false; fixture::volatileServer = true;
    api::capture(real_shaped_callback(8), fixture::platform); api::service(40001);
    assert(fixture::posts == 8 && api::published() && !api::durably_published());
    fixture::volatileServer = false; fixture::badAck = true;
    api::capture(real_shaped_callback(9), fixture::platform); api::service(40002);
    assert(fixture::posts == 9 && !api::published() && !api::durably_published());
    api::reset(); assert(!api::durably_published());
    std::puts("PASS: authenticated public publication, bounded retries, restart republish, generation and identity ACK ownership");
}
