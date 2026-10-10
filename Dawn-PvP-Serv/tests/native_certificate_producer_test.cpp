#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "middleware/bap/native_certificate_publication.h"
#include "middleware/bap/native_certificate.h"
#include "middleware/bap/certificate.h"
#include "server/transport/native_certificate_cache.h"
#include "server/transport/native_certificate_response.h"

namespace wire = dawn::middleware::bap::native_certificate;
namespace proto = dawn::middleware::protobuf;
namespace cache = dawn::server::transport::native_certificate_cache;
constexpr std::uint64_t kPlatform = 0x01100001F0000003ULL;

void fixed(std::vector<std::byte>& output, unsigned field, unsigned size, std::uint64_t value) {
    output.push_back(static_cast<std::byte>((field << 3) | (size == 8 ? 1 : 5)));
    for (unsigned i = 0; i < size; ++i) output.push_back(static_cast<std::byte>(value >> (i * 8)));
}
void integer(std::vector<std::byte>& output, unsigned field, std::uint64_t value) {
    std::array<std::byte, 32> bytes{}; proto::Writer writer(bytes);
    assert(writer.write_varint(field, value)); output.insert(output.end(), bytes.begin(), bytes.begin() + writer.size());
}
void bytes(std::vector<std::byte>& output, unsigned field, std::span<const std::byte> value) {
    std::array<std::byte, 1024> bytes{}; proto::Writer writer(bytes);
    assert(writer.write_length_delimited(field, value)); output.insert(output.end(), bytes.begin(), bytes.begin() + writer.size());
}
std::vector<std::byte> certificate(unsigned keyByte = 3, std::uint64_t platform = kPlatform,
                                   unsigned expiry = 2000, unsigned app = wire::kAppId) {
    std::vector<std::byte> output;
    integer(output, 1, 1); std::array<std::byte, 32> key{};
    key.fill(static_cast<std::byte>(keyByte)); bytes(output, 2, key);
    fixed(output, 4, 8, platform); fixed(output, 8, 4, 900); fixed(output, 9, 4, expiry);
    integer(output, 10, app); return output;
}
std::array<std::byte, 792> callback(std::span<const std::byte> cert) {
    std::array<std::byte, 792> output{};
    const std::uint32_t result = 1, count = static_cast<std::uint32_t>(cert.size()), signature = 64;
    const std::uint64_t ca = 0xb68350cff5aa391bULL;
    std::memcpy(output.data(), &result, 4); std::memcpy(output.data() + 4, &count, 4);
    std::copy(cert.begin(), cert.end(), output.begin() + 8);
    std::memcpy(output.data() + 0x208, &ca, 8); std::memcpy(output.data() + 0x210, &signature, 4);
    std::fill_n(output.begin() + 0x214, 64, std::byte{0x56});
    // private callback regions differ on purpose and must never enter the public envelope
    std::fill(output.begin() + 0x298, output.end(), std::byte{0xAD});
    return output;
}
std::vector<std::byte> envelope(std::span<const std::byte> cert) {
    auto input = callback(cert); std::array<std::byte, wire::kCapacity> output{}; std::size_t size{};
    assert(wire::from_callback(input, kPlatform, 1000, output, size));
    return {output.begin(), output.begin() + size};
}
std::vector<std::byte> request(unsigned keyByte = 3) {
    const auto cert = certificate(keyByte);
    std::vector<std::byte> csr, svc; bytes(csr, 1, cert); bytes(svc, 3, csr); return svc;
}
int main() {
    wire::Metadata metadata{};
    auto cert = certificate(), signedCert = envelope(cert);
    assert(wire::signed_metadata(signedCert, kPlatform, 1000, metadata));
    assert(metadata.platform == kPlatform && metadata.key[0] == std::byte{3} && metadata.expires == 2000);
    assert(std::find(signedCert.begin(), signedCert.end(), std::byte{0xAD}) == signedCert.end());
    auto unsafe = signedCert; bytes(unsafe, 1, std::array<std::byte, 32>{});
    assert(!wire::signed_metadata(unsafe, kPlatform, 1000, metadata));
    unsafe = signedCert; bytes(unsafe, 4, cert);
    assert(!wire::signed_metadata(unsafe, kPlatform, 1000, metadata));
    assert(!wire::signed_metadata(signedCert, kPlatform + 1, 1000, metadata));
    assert(!wire::signed_metadata(signedCert, kPlatform, 2000, metadata));
    assert(!wire::certificate_metadata(certificate(3, kPlatform, 2000, 1), kPlatform, 1000, metadata));
    auto input = callback(cert); std::array<std::byte, wire::kCapacity> output{}; std::size_t size{};
    input[0] = std::byte{2}; assert(!wire::from_callback(input, kPlatform, 1000, output, size));
    input = callback(cert); input[0x210] = std::byte{63}; assert(!wire::from_callback(input, kPlatform, 1000, output, size));
    input = callback(cert); assert(!wire::from_callback(input, kPlatform, 1000, std::span(output).first(8), size));
    cache::clear(); assert(cache::publish(kPlatform, signedCert, 1000));
    cache::Snapshot found{};
    assert(cache::find(kPlatform, request(), 1000, found) && found.size == signedCert.size());
    assert(std::equal(signedCert.begin(), signedCert.end(), found.bytes.begin()));
    assert(!cache::find(kPlatform + 1, request(), 1000, found));
    assert(!cache::find(kPlatform, request(4), 1000, found));
    const auto rotated = envelope(certificate(4));
    assert(cache::publish(kPlatform, rotated, 1000));
    assert(cache::find(kPlatform, request(), 1000, found));
    assert(cache::find(kPlatform, request(4), 1000, found));
    const auto renewal = envelope(certificate(4, kPlatform, 4000));
    assert(cache::publish(kPlatform, renewal, 1000));
    assert(cache::publish(kPlatform, rotated, 1000));
    assert(cache::find(kPlatform, request(4), 3000, found) && found.size == renewal.size());
    assert(std::equal(renewal.begin(), renewal.end(), found.bytes.begin()));
    assert(!cache::find(kPlatform, request(), 3000, found));
    auto malformed = request(); malformed.push_back(std::byte{0x80});
    assert(!cache::find(kPlatform, malformed, 1000, found));
    assert(dawn::middleware::bap::certificate::encode_signed_response(signedCert, output, size));
    proto::Reader response(std::span(output).first(size)); proto::Field field{}; bool exact = false;
    while (response.next(field)) if (field.fieldNumber == 3) exact = std::equal(field.bytes.begin(), field.bytes.end(), signedCert.begin(), signedCert.end());
    assert(exact && response.remaining() == 0);
    namespace certificateResponse = dawn::server::transport::native_certificate_response;
    cache::Snapshot selected{};
    selected.size = signedCert.size();
    std::copy(signedCert.begin(), signedCert.end(), selected.bytes.begin());
    assert(certificateResponse::encode(request(), true, selected, output, size) == 200 && size != 0);
    assert(certificateResponse::encode(request(), false, selected, output, size) == 503 && size == 0);
    assert(certificateResponse::encode(request(4), false, selected, output, size) == 503 && size == 0);
    assert(certificateResponse::encode(malformed, false, selected, output, size) == 400 && size == 0);
    assert(certificateResponse::encode({}, false, selected, output, size) == 400 && size == 0);
    assert(certificateResponse::encode(request(), true, selected, {}, size) == 503 && size == 0);
    selected.size = selected.bytes.size() + 1;
    assert(certificateResponse::encode(request(), true, selected, output, size) == 503 && size == 0);
    cache::clear();
    for (unsigned key = 1; key <= 140; ++key)
        assert(cache::publish(kPlatform, envelope(certificate(key)), 1000));
    assert(cache::find(kPlatform, request(1), 1000, found)
        && cache::find(kPlatform, request(140), 1000, found));
    cache::clear();
    wchar_t temporary[MAX_PATH]{}; assert(GetTempPathW(MAX_PATH, temporary) != 0);
    const std::wstring directory = std::wstring(temporary) + L"dawn-public-cert-test-"
        + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    assert(CreateDirectoryW(directory.c_str(), nullptr));
    assert(!cache::configure_storage(L"relative"));
    assert(cache::configure_storage(directory));
    bool durable{};
    assert(cache::publish(kPlatform, signedCert, 1000, &durable) && durable);
    assert(cache::publish(kPlatform, renewal, 1000, &durable) && durable);
    cache::clear();
    assert(cache::find(kPlatform, request(), 1000, found));
    assert(std::equal(signedCert.begin(), signedCert.end(), found.bytes.begin()));
    assert(!cache::find(kPlatform + 1, request(), 1000, found));
    assert(!cache::find(kPlatform, request(5), 1000, found));
    assert(cache::publish(kPlatform, rotated, 1000, &durable) && durable);
    cache::clear(); assert(cache::find(kPlatform, request(4), 3000, found));
    assert(std::equal(renewal.begin(), renewal.end(), found.bytes.begin()));
    assert(!cache::find(kPlatform, request(), 3000, found));
    WIN32_FIND_DATAW row{};
    HANDLE listing = FindFirstFileW((directory + L"\\*.cert").c_str(), &row); assert(listing != INVALID_HANDLE_VALUE);
    do {
        const auto path = directory + L"\\" + row.cFileName;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, nullptr);
        assert(file != INVALID_HANDLE_VALUE);
        std::array<std::byte, 4> corrupt{}; DWORD written{};
        assert(WriteFile(file, corrupt.data(), static_cast<DWORD>(corrupt.size()), &written, nullptr));
        CloseHandle(file);
    } while (FindNextFileW(listing, &row)); FindClose(listing);
    cache::clear(); assert(!cache::find(kPlatform, request(), 1000, found));
    listing = FindFirstFileW((directory + L"\\*").c_str(), &row); assert(listing != INVALID_HANDLE_VALUE);
    do { if ((row.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        assert(DeleteFileW((directory + L"\\" + row.cFileName).c_str()));
    } while (FindNextFileW(listing, &row)); FindClose(listing);
    assert(RemoveDirectoryW(directory.c_str()));
    assert(cache::publish(kPlatform, signedCert, 1000, &durable) && !durable);
    assert(cache::find(kPlatform, request(), 1000, found));
    assert(cache::configure_storage(L""));
    namespace publication = dawn::middleware::bap::native_certificate_publication;
    assert(publication::requested(publication::kRequest));
    assert(publication::acknowledgement(publication::kDurableAck, durable) && durable);
    assert(publication::acknowledgement(publication::kVolatileAck, durable) && !durable);
    assert(!publication::acknowledgement({}, durable) && !durable);
    auto unknown = publication::kDurableAck; unknown[3] = std::byte{2};
    assert(!publication::acknowledgement(unknown, durable));
    std::puts("PASS: public-only Valve extraction, authenticated identity/key/expiry selection, overlapping rotations, exact svc305 and native400/503 refusals");
}
