#include "egress_dns_replacements.h"

#include <intrin.h>
#include <string_view>

#include "../internal.h"
#include "../policy/policy.h"

namespace dawn::client::hooks::egress::dns {
namespace {

constexpr DNS_STATUS kBlockedDnsStatus = DNS_ERROR_RCODE_REFUSED;
constexpr wchar_t kLoopbackNameW[] = L"localhost";
// cache and local tables only, so a substituted query can never reach the wire
constexpr DWORD kLocalOnlyOptions = DNS_QUERY_NO_WIRE_QUERY | DNS_QUERY_NO_NETBT;
constexpr DWORD kConflictingOptions = DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_LOCAL_NAME
                                      | DNS_QUERY_NO_HOSTS_FILE | DNS_QUERY_WIRE_ONLY;

// a caller-supplied server list clashes with a cache-only query and the resolver returns
// ERROR_INVALID_PARAMETER instead of answering locally
[[nodiscard]] DWORD local_options(DWORD options, PVOID extra) noexcept {
    const DWORD cleared = options & ~kConflictingOptions;
    return extra != nullptr ? cleared : cleared | kLocalOnlyOptions;
}

// an address literal needs no lookup and cannot name a remote host, so pass it through;
// replacing it is what produced the rejected query
template <typename Character> [[nodiscard]] bool is_numeric(const Character* name) noexcept {
    return name != nullptr && ((name[0] >= '0' && name[0] <= '9') || name[0] == ':');
}

template <typename Character>
[[nodiscard]] std::basic_string_view<Character> view(const Character* name) noexcept {
    return name != nullptr ? std::basic_string_view<Character>(name)
                           : std::basic_string_view<Character>{};
}

template <typename Character>
[[nodiscard]] DNS_STATUS deny_query(const Character* name, WORD type, const void* caller) noexcept {
    policy::log_name_decision(policy::NameOperation::dns, false, view(name), type, 0, caller);
    return kBlockedDnsStatus;
}

template <typename Character>
[[nodiscard]] DNS_STATUS
allow_query(DNS_STATUS status, const Character* name, WORD type, const void* caller) noexcept {
    policy::log_name_decision(
        policy::NameOperation::dns, true, view(name), type, static_cast<unsigned>(status), caller);
    return status;
}

} // namespace

DNS_STATUS WINAPI query_ex(PDNS_QUERY_REQUEST request,
                           PDNS_QUERY_RESULT results,
                           PDNS_QUERY_CANCEL cancel) noexcept {
    const void* const caller = _ReturnAddress();
    const auto call = original<decltype(&::DnsQueryEx)>(HookSlot::dnsQueryEx);
    if (request == nullptr || request->QueryName == nullptr || call == nullptr) {
        if (results != nullptr) {
            results->QueryStatus = kBlockedDnsStatus;
            results->QueryOptions = 0;
            results->pQueryRecords = nullptr;
            results->Reserved = nullptr;
        }
        if (cancel != nullptr) {
            *cancel = {};
        }
        return deny_query(request != nullptr ? request->QueryName : nullptr,
                          request != nullptr ? request->QueryType : 0,
                          caller);
    }
    DNS_QUERY_REQUEST forwarded = *request;
    if (!is_numeric(request->QueryName)) {
        forwarded.QueryName = kLoopbackNameW;
        forwarded.QueryOptions =
            local_options(static_cast<DWORD>(request->QueryOptions), request->pDnsServerList);
    }
    return allow_query(
        call(&forwarded, results, cancel), request->QueryName, request->QueryType, caller);
}

} // namespace dawn::client::hooks::egress::dns
