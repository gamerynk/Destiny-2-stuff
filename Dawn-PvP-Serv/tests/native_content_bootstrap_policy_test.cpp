#include "client/content/bootstrap/bootstrap_token_publish.h"
#include "client/hooks/external_server/transport.h"
#include "client/targets/game/network.h"
#include "core/identity/device_identity.h"
#include "core/settings/settings.h"
#include "core/logging/log.h"
#include "middleware/content/bootstrap_configuration.h"
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace wire = dawn::middleware::content::bootstrap_configuration;
constexpr std::string_view kGuid = "01234567-89ab-cdef-0123-456789abcdef";
constexpr std::string_view kUrl = "https://configured.example/config/?exact=1";
enum class Mode { native, guidOnly, legacy, unauthorized, legacyFalseAck };
Mode mode{};
unsigned calls{};
unsigned localPublications{};
bool localPublish = true;
bool targetPresent = true;
bool guidPublish = true;
std::array<std::byte,16> token{};
void require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
}
namespace dawn::core::settings {
const Settings& get() noexcept {
    static const Settings settings=[] {
        Settings value{}; value.client.externalServer.enabled=true;
        std::copy(kUrl.begin(),kUrl.end(),value.client.externalServer.configUrl.begin());
        return value;
    }();
    return settings;
}
}
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::state {
bool publish_bootstrap_token(std::span<const std::byte> value) noexcept {
    ++localPublications;
    require(value.size()==token.size() && std::equal(value.begin(),value.end(),token.begin()),
        "local package reader receives the exact installed token");
    return localPublish;
}
}
namespace dawn::client::targets::game::network {
const Targets& get() noexcept { static Targets targets{}; targets.contentIdToken=targetPresent?token.data():nullptr; return targets; }
}
namespace dawn::client::hooks::config_getter {
bool publish_server_guid(std::string_view guid) noexcept { return guidPublish && guid==kGuid; }
}
namespace dawn::client::hooks::external_server {
bool consume(const client::network::HttpRequest& request,client::network::HttpResponse& response) noexcept {
    ++calls; require(request.url=="/v1/bootstrap" && request.method==client::network::HttpMethod::post,"exact bootstrap route");
    const bool legacy=request.body.size()==wire::kTokenSize;
    if (!legacy) {
        wire::View view{}; require(wire::parse(request.body,view) && view.requestsNativeAcknowledgment && view.url==kUrl,"V2 exact URL metadata");
        namespace identity=dawn::core::identity::device_identity;
        identity::Claims claims{};
        const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        require(identity::verify(identity::Scope::contentBootstrap,view.proof,static_cast<std::uint64_t>(now),claims),"actual scope14 proof precedes policy");
        if (mode==Mode::legacy || mode==Mode::legacyFalseAck) { response.statusCode=400;return true; }
        if (mode==Mode::unauthorized) { response.statusCode=401;return true; }
    } else require(mode==Mode::legacy || mode==Mode::legacyFalseAck,"fallback only when metadata unsupported");
    response.statusCode=200;
    require(wire::acknowledge(mode==Mode::native || mode==Mode::legacyFalseAck,kGuid,request.response,response.size),"bounded ACK producer");
    return true;
}
}
int main() {
    namespace bootstrap=dawn::client::content::bootstrap;
    namespace identity=dawn::core::identity::device_identity;
    require(identity::initialize(GetModuleHandleW(nullptr),0x0110000112345678ULL),"own-process signed identity");
    for (auto testMode:{Mode::native,Mode::guidOnly,Mode::legacy,Mode::legacyFalseAck}) {
        mode=testMode;calls=0;localPublications=0;
        require(bootstrap::publish_token(),"supported publication succeeds");
        require(localPublications==1,"external server mode preserves local package readiness");
        require(bootstrap::native_configuration_ready()==(mode==Mode::native),"only authenticated V2 native ACK removes getters");
        require(calls==((mode==Mode::legacy || mode==Mode::legacyFalseAck)?2U:1U),"bounded legacy fallback ordering");
    }
    mode=Mode::native;require(bootstrap::publish_token() && bootstrap::native_configuration_ready(),"native generation selected");
    localPublish=false;calls=0;
    require(!bootstrap::publish_token() && !bootstrap::native_configuration_ready() && calls==0,
        "local publication failure cannot select native configuration or publish remotely");
    localPublish=true;
    targetPresent=false;require(!bootstrap::publish_token() && !bootstrap::native_configuration_ready(),"failed next generation clears readiness");targetPresent=true;
    guidPublish=false;require(!bootstrap::publish_token() && !bootstrap::native_configuration_ready(),"GUID publication failure cannot select native");guidPublish=true;
    mode=Mode::unauthorized;calls=0;
    require(!bootstrap::publish_token() && !bootstrap::native_configuration_ready() && calls==8,
        "authentication refusal is never downgraded to unauthenticated legacy publication");
    std::array<std::byte,wire::kAcknowledgmentSize+1> bytes{};std::size_t written{};
    require(wire::acknowledge(true,kGuid,bytes,written) && written==40,"native ACK fixed framing");
    std::string_view guid{};bool native{};
    for (std::size_t length=0;length<40;++length) {
        if(length==36) continue;
        require(!wire::parse_acknowledgment(std::span(bytes).first(length),guid,native) && !native,"truncated ACK never selects native");
    }
    require(!wire::parse_acknowledgment(bytes,guid,native),"extra ACK byte rejected");
    bytes[3]=std::byte{2};require(!wire::parse_acknowledgment(std::span(bytes).first(40),guid,native),"unknown ACK version rejected");
    std::array<std::byte,wire::kCapacity> metadata{};std::array<std::byte,200> proof{};
    require(wire::encode(token,proof,kUrl,metadata,written),"metadata V2 encoded");
    metadata[3]=std::byte{1};wire::View view{};
    require(wire::parse(std::span(metadata).first(written),view) && !view.requestsNativeAcknowledgment,"existing V1 clients retain36-byte reply");
    metadata[3]=std::byte{3};require(!wire::parse(std::span(metadata).first(written),view),"unknown metadata version rejected");
    identity::shutdown();std::puts("native content bootstrap compatibility policy passed");
}
