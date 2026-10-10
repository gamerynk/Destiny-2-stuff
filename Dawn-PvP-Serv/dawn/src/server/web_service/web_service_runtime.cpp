#include "web_service_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../middleware/encoding/bit_reader.h"
#include "../../middleware/encoding/byte_order.h"
#include "../../middleware/web_service/messages/opcode1901.h"
#include "../../middleware/web_service/messages/opcode205.h"
#include "../../middleware/web_service/messages/opcode206.h"
#include "../../middleware/web_service/messages/opcode501_codec.h"
#include "../../middleware/web_service/messages/opcode503.h"
#include "../../middleware/web_service/messages/opcode504.h"
#include "../../middleware/web_service/messages/opcode601/opcode601_codec.h"
#include "../../middleware/web_service/messages/opcode901/opcode901_codec.h"
#include "../../middleware/web_service/messages/opcode903.h"
#include "../../middleware/web_service/web_service_envelope.h"
#include "../../state/account/account_state.h"
#include "../../state/build_data/runtime.h"
#include "../../state/runtime/runtime.h"
#include "opcode_routes.h"
#include "web_service_actions.h"
#include "settings_save.h"

namespace dawn::server::web_service {

constexpr std::size_t kOpcodeLineCapacity = 64;
constexpr std::size_t kRequestPayloadTraceBytes = 192;
constexpr std::string_view kTruncated = " truncated=1";
constexpr std::uint16_t kEquipOpcode = 403;
constexpr std::uint16_t kUnequipOpcode = 404;
constexpr std::uint16_t kItemStateOpcode = 406;
constexpr std::uint16_t kItemDismantleOpcode = 402;
constexpr std::uint16_t kItemAcquisitionOpcode = 1820;
constexpr std::size_t kNoMutation = 0;
// the descriptor biases logical zero to the wire success the client expects, so any other value is a
// refusal; its five bits hold no error taxonomy, the log line names the reason
constexpr std::int32_t kRefusedStatus = 1;

void report_bootstrap_normalization(std::uint64_t claimedSoid,
                                    std::uint64_t accountSoid) noexcept {
    if (claimedSoid == accountSoid) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws503 stage=account_key result=normalized claimed=0x%llX account=0x%llX",
        static_cast<unsigned long long>(claimedSoid),
        static_cast<unsigned long long>(accountSoid));
    if (count > 0 && static_cast<std::size_t>(count) < line.size()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

void report_request(const middleware::web_service::Message& message) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=ws stage=request opcode=%u transaction=%u payload_bytes=%zu payload_hex=",
                      static_cast<unsigned>(message.opcode),
                      static_cast<unsigned>(message.transactionId),
                      message.payload.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }

    std::size_t length = static_cast<std::size_t>(prefix);
    const std::size_t traced =
        (std::min)(message.payload.size(), static_cast<std::size_t>(kRequestPayloadTraceBytes));
    (void)core::log::append_hex(line, length, message.payload.first(traced));
    if (traced != message.payload.size() && length + kTruncated.size() < line.size()) {
        std::memcpy(line.data() + length, kTruncated.data(), kTruncated.size());
        length += kTruncated.size();
    }
    if (length != 0) {
        core::log::write(core::log::Channel::server, core::log::Level::info, {line.data(), length});
    }
}

constexpr std::size_t kPurchaseLineCapacity = 128;
constexpr std::int32_t kPurchaseRefusedCode = 1;

[[nodiscard]] std::int64_t server_clock_seconds() noexcept {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch).count();
}

// no award, cost or stock rule exists yet so nothing can succeed; the refusal must still be answered
// or it holds the head of the client's pending queue
[[nodiscard]] bool refuse_purchase(const middleware::web_service::Message& message,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept {
    namespace purchase_codec = middleware::web_service::messages::opcode901;
    purchase_codec::Request purchase;
    const bool parsed = purchase_codec::parse_request(message, purchase);
    const auto policy = purchase_codec::check_clock(purchase, server_clock_seconds());
    std::array<char, kPurchaseLineCapacity> line{};
    const int length =
        parsed ? std::snprintf(
                     line.data(),
                     line.size(),
                     "ev=ws901 stage=purchase result=refuse vendor=%d sale=%d present=%u policy=%s",
                     static_cast<int>(purchase.vendorIndex),
                     static_cast<int>(purchase.saleIndex),
                     purchase.hasClock ? 1U : 0U,
                     purchase_codec::clock_policy_name(policy))
               : std::snprintf(line.data(),
                               line.size(),
                               "ev=ws901 stage=purchase result=refuse reason=parse");
    if (length > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         {line.data(), static_cast<std::size_t>(length)});
    }
    middleware::web_service::StatusResponse status{};
    status.code = kPurchaseRefusedCode;
    status.trailingBool = false;
    return middleware::web_service::encode_response(
        message,
        middleware::web_service::ResponseShape::statusPairWithBool,
        status,
        response,
        written);
}

// the client matches on the echoed transaction id; a missing body under-runs the decoder and takes
// the BAP connection down
bool encode_echo(const middleware::web_service::Message& message,
                 std::span<std::byte> response,
                 std::size_t& written) noexcept {
    std::array<char, kOpcodeLineCapacity> line{};
    const int count = std::snprintf(
        line.data(), line.size(), "ev=ws stage=body result=echo opcode=%u", message.opcode);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    namespace ws = middleware::web_service;
    return ws::encode_response(
        message, ws::ResponseShape::generic, ws::StatusResponse{}, response, written);
}

bool consume(std::span<const std::byte> request,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    // the mutation variant owns full before/after inventory images; keep it in thread storage so this
    // path can't add its largest alternative to the BAP worker stack
    thread_local Outcome outcome;
    return consume(request, response, written, outcome);
}

bool consume(std::span<const std::byte> request,
             std::span<std::byte> response,
             std::size_t& written,
             Outcome& outcome) noexcept {
    written = 0;
    reset(outcome);
    middleware::web_service::Message message;
    if (!middleware::web_service::parse_request(request, message)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws stage=parse result=fail");
        return false;
    }
    report_request(message);
    if (message.opcode == middleware::web_service::messages::opcode701::kOpcode)
        return save_settings(message, response, written);

    if (message.opcode == middleware::web_service::messages::opcode205::kOpcode) {
        const auto investment = state::investment_snapshot();
        return middleware::web_service::messages::opcode205::encode_response(
                   message, investment, response, written)
               || encode_echo(message, response, written);
    }

    if (message.opcode == middleware::web_service::messages::opcode503::kOpcode) {
        middleware::web_service::messages::opcode503::Request bootstrap;
        const bool parsed =
            middleware::web_service::messages::opcode503::parse_request(message, bootstrap);
        const state::AccountState account = state::account_snapshot();
        if (!parsed || !state::account::valid(account) || account.primarySoid == 0) {
            return encode_echo(message, response, written);
        }
        // retail derives the request field from local platform identity, so it's only a routing
        // hint; echoing it rooted other accounts' QueueZ families on a foreign SOID
        report_bootstrap_normalization(bootstrap.primarySoid, account.primarySoid);
        bootstrap.primarySoid = account.primarySoid;
        bootstrap.hasPrimarySoid = true;
        const auto investment = state::investment_snapshot();
        if (!middleware::web_service::messages::opcode503::encode_response(
                message, bootstrap, investment, response, written)) {
            return encode_echo(message, response, written);
        }
        return true;
    }

    if (message.opcode == middleware::web_service::messages::opcode501::kOpcode) {
        const std::uint64_t characterSoid =
            state::account::selected_character_soid(state::account_snapshot());
        return middleware::web_service::messages::opcode501::encode_response(
                   message,
                   (std::numeric_limits<std::int32_t>::min)(),
                   characterSoid,
                   response,
                   written)
               || encode_echo(message, response, written);
    }

    if (message.opcode == 502) {
        std::uint64_t characterSoid = 0;
        if (message.payload.size() >= 8) {
            middleware::encoding::bits::Reader reader(message.payload);
            if (reader.read(64, characterSoid)) {
                (void)state::delete_character(characterSoid);
            }
        }
        return encode_echo(message, response, written);
    }

    if (message.opcode == middleware::web_service::messages::opcode901::kOpcode) {
        return refuse_purchase(message, response, written)
               || encode_echo(message, response, written);
    }

    if (message.opcode == middleware::web_service::messages::opcode601::kOpcode) {
        return middleware::web_service::messages::opcode601::encode_response(
                   message, response, written);
    }

    middleware::queuez::Subscription subscription;
    const bool subscribes =
        message.opcode == middleware::web_service::messages::opcode206::kOpcode
        && middleware::web_service::messages::opcode206::parse_request(message, subscription);

    // the action runs before the reply is encoded because the reply reports whether it worked;
    // an empty outcome means refused. The caller publishes once the response is framed
    bool dispatched = true;
    if (message.opcode == middleware::web_service::messages::opcode504::kOpcode) {
        select_character(message, outcome);
    } else if (message.opcode == kItemDismantleOpcode) {
        dismantle_item(message, outcome);
    } else if (message.opcode == kEquipOpcode) {
        mutate_equipment(message, false, outcome);
    } else if (message.opcode == kUnequipOpcode) {
        mutate_equipment(message, true, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode903::kOpcode) {
        mutate_socket_plug(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode1901::kOpcode) {
        mutate_equipped_socket_plug(message, outcome);
    } else if (message.opcode == kItemStateOpcode) {
        mutate_item_state(message, outcome);
    } else if (message.opcode == kItemAcquisitionOpcode) {
        acquire_item(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode405::kOpcode) {
        transfer_item(message, outcome);
    } else if (message.opcode == 801 || message.opcode == 802) {
        select_socket_entry(message, outcome);
    } else {
        dispatched = false;
    }
    const bool prepared = outcome.hasSelectedCharacter || outcome.mutation.index() != kNoMutation;

    middleware::web_service::ResponseShape shape{};
    resolve_response_shape(message.opcode, shape);
    middleware::web_service::StatusResponse status{};
    if (dispatched && !prepared) {
        status.code = kRefusedStatus;
    }
    if (!middleware::web_service::encode_response(message, shape, status, response, written)) {
        reset(outcome);
        return encode_echo(message, response, written);
    }
    if (subscribes) {
        outcome.hasSubscription = true;
        outcome.subscription = subscription;
    }
    return true;
}

} // namespace dawn::server::web_service
