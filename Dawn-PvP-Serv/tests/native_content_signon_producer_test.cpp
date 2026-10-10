#include "middleware/content/bootstrap_configuration.h"
#include "middleware/signon/config/signon_config_blob.h"
#include "server/http/content_configuration.h"
#include "core/settings/settings.h"
#include "state/content_manifest/content_manifest_state_runtime.h"
#include "core/identity/device_identity.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace wire = dawn::middleware::content::bootstrap_configuration;
namespace cache = dawn::server::http::content_configuration;
constexpr std::string_view kUrl = "http://configured.example/config/";
constexpr std::string_view kGuid = "01234567-89ab-cdef-0123-456789abcdef";
namespace dawn::core::settings {
const Settings& get() noexcept { static const Settings settings{}; return settings; }
}
namespace dawn::state::content_manifest {
bool visit_snapshot(SnapshotVisitor visitor, void* context) noexcept {
    std::array<std::byte, kFingerprintSize> fingerprint{};
    return visitor(context, View{{}, kGuid, fingerprint});
}
}
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::uint32_t fixed(std::span<const std::byte> bytes, std::size_t& at) {
    require(at + 4 <= bytes.size(), "bounded fixed32"); std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= std::to_integer<std::uint32_t>(bytes[at++]) << shift;
    return value;
}
std::string_view string(std::span<const std::byte> bytes, std::size_t& at) {
    require(at < bytes.size(), "bounded native string length");
    const auto size = std::to_integer<std::size_t>(bytes[at++]);
    require(size < 128 && size <= bytes.size() - at, "bounded native string bytes");
    std::string_view value(reinterpret_cast<const char*>(bytes.data() + at), size); at += size;
    return value;
}
int main(int argc, char** argv) {
    namespace identity = dawn::core::identity::device_identity;
    require(identity::initialize(GetModuleHandleW(nullptr),0x0110000112345678ULL),
        "own-test-process installation identity initialization");
    std::array<std::byte,identity::kProofSize> signedProof{};
    identity::Claims claims{};
    require(identity::issue(identity::Scope::contentBootstrap,10000,signedProof)
        && identity::verify(identity::Scope::contentBootstrap,signedProof,10001,claims)
        && claims.deviceId!=0 && claims.platformId==0x0110000112345678ULL,
        "genuine scoped signed bootstrap proof");
    require(!identity::verify(identity::Scope::signOn,signedProof,10001,claims),
        "bootstrap proof cannot authorize SignOn");
    signedProof[16]^=std::byte{1};
    require(!identity::verify(identity::Scope::contentBootstrap,signedProof,10001,claims),
        "public identity tampering rejected");
    signedProof[16]^=std::byte{1};
    require(!identity::verify(identity::Scope::contentBootstrap,signedProof,10901,claims),
        "expired bootstrap proof rejected");
    require(identity::issue(identity::Scope::transportCertificate,10000,signedProof)
        && identity::verify(identity::Scope::transportCertificate,signedProof,10001,claims)
        && claims.deviceId!=0 && claims.platformId==0x0110000112345678ULL,
        "genuine scoped signed transport-certificate proof");
    require(!identity::verify(identity::Scope::contentBootstrap,signedProof,10001,claims)
        && !identity::verify(identity::Scope::signOn,signedProof,10001,claims),
        "certificate proof cannot authorize content publication or SignOn");
    signedProof[16]^=std::byte{1};
    require(!identity::verify(identity::Scope::transportCertificate,signedProof,10001,claims),
        "certificate identity tampering rejected");
    signedProof[16]^=std::byte{1};
    require(!identity::verify(identity::Scope::transportCertificate,signedProof,10901,claims),
        "expired certificate proof rejected");
    identity::shutdown();
    std::array<std::byte,16> token{};
    for (std::size_t index = 0; index < token.size(); ++index) token[index] = static_cast<std::byte>(index);
    std::array<std::byte,200> proof{};
    std::array<std::byte,wire::kCapacity> metadata{}; std::size_t size{};
    require(wire::encode(token, proof, kUrl, metadata, size), "bounded bootstrap envelope");
    wire::View parsed{};
    require(wire::parse(std::span(metadata).first(size), parsed)
        && parsed.url == kUrl && parsed.proof.size() == proof.size()
        && std::equal(parsed.token.begin(), parsed.token.end(), token.begin()), "round trip");
    for (std::size_t cut = 0; cut < size; ++cut)
        require(!wire::parse(std::span(metadata).first(cut), parsed), "all truncations fail closed");
    require(!wire::parse(std::span(metadata).first(size+1), parsed), "trailing body rejected");
    for (auto url : {"", "ftp://configured.example/config/", "http:///config/", "http://user@host/config/",
        "http://host/\r\nInjected: x", "http://host/ space", "http://host/\\config"})
        require(!wire::valid_url(url), "invalid URL rejected");
    require(wire::valid_url("HTTPS://configured.example?query#fragment"), "HTTP scheme case and suffix preserved");
    cache::clear(); cache::Snapshot snapshot{};
    require(cache::snapshot(1,2,snapshot) && std::string_view(snapshot.guid.data())==kGuid,
        "old client canonical manifest fallback");
    const auto fallback = snapshot.url;
    require(cache::publish(11,22,kUrl) && cache::snapshot(11,22,snapshot)
        && std::string_view(snapshot.url.data())==kUrl, "authenticated pair publication");
    require(cache::snapshot(12,22,snapshot) && snapshot.url==fallback, "device identity separation");
    require(cache::snapshot(11,23,snapshot) && snapshot.url==fallback, "platform identity separation");
    require(!cache::publish(11,0,kUrl) && !cache::publish(11,22,"http:///bad"), "invalid cache publication");
    for (std::uint64_t platform=1000; platform<2000; ++platform)
        require(cache::publish(platform,platform,kUrl), "all authenticated devices admitted without a new fixed cap");
    require(cache::publish(9999,9999,kUrl), "additional device does not replace an existing identity");
    require(cache::publish(11,22,"https://another.example/config/")
        && cache::snapshot(11,22,snapshot)
        && std::string_view(snapshot.url.data())=="https://another.example/config/", "existing identity can refresh populated cache");
    cache::clear(); require(cache::snapshot(11,22,snapshot) && snapshot.url==fallback, "server generation clears cache");
    namespace blob = dawn::middleware::signon::config;
    std::array<std::byte,blob::kBlobCapacity> output{}; std::size_t written{};
    require(blob::build(token,kUrl,kGuid,output,written), "native SignOn full producer");
    auto bytes=std::span(output).first(written); std::size_t at{};
    require(fixed(bytes,at)==0x123456 && fixed(bytes,at)==0x7B620F, "native blob/token key");
    require(string(bytes,at).size()==16, "token remains16");
    require(fixed(bytes,at)==0x206,"existing event priorities retained"); at+=22*4;
    require(fixed(bytes,at)==0x1A39E5,"existing urgency threshold retained"); at+=4;
    require(fixed(bytes,at)==0x64 && string(bytes,at)==kUrl,"native primary URL");
    require(fixed(bytes,at)==0x65 && string(bytes,at)==kGuid,"canonical manifest GUID");
    require(fixed(bytes,at)==0x66 && string(bytes,at)==kUrl,"native alternate URL");
    require(fixed(bytes,at)==0x37E && bytes[at++]==std::byte{},"native content enabled");
    require(fixed(bytes,at)==0 && at==written,"one final terminator");
    require(!blob::build(token,kUrl,kGuid,std::span(output).first(written-1),size) && size==0,
        "exact capacity checked before mutation");
    require(!blob::build(token,kUrl,"bad-guid",output,size),"GUID validation");
    if(argc>1) { std::ofstream file(argv[1],std::ios::binary);
        file.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(written));
        require(file.good(),"write nonsecret production native replay fixture"); }
    std::puts("native content SignOn producer tests passed");
}
