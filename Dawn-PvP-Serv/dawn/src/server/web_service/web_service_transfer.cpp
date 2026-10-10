// opcode 405 vault transfer; the direction isn't trusted from the wire: an instance already in the
// vault is pulled to the selected character, anything else is pushed into the vault

#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../middleware/encoding/bit_reader.h"
#include "../../state/build_data/runtime.h"
#include "web_service_actions.h"

namespace dawn::server::web_service {
namespace {

namespace opcode405 = middleware::web_service::messages::opcode405;

void report_transfer(const middleware::web_service::Message& message,
                     const opcode405::Request& request,
                     std::string_view result,
                     std::string_view reason,
                     const char* direction) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws405 stage=prepare result=%.*s reason=%.*s direction=%s transaction=%u "
        "payload_bytes=%zu unresolved16=%d unresolved8=%d instance=0x%llX definition=%d "
        "quantity=%d",
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        direction,
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        static_cast<int>(request.unresolved16),
        static_cast<int>(request.unresolved8),
        static_cast<unsigned long long>(request.itemInstanceSoid),
        static_cast<int>(request.definitionIndex),
        static_cast<int>(request.quantity));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         result == "ok" ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

} // namespace

// decodes {u64 item, u8 socket index biased by 128} plus alignment; only opcode 801 on the equipped
// subclass prepares the pick, anything else leaves the outcome empty, which answers a refusal
void select_socket_entry(const middleware::web_service::Message& message,
                         Outcome& outcome) noexcept {
    middleware::encoding::bits::Reader reader(message.payload);
    std::uint64_t instance = 0;
    std::uint64_t biasedIndex = 0;
    const bool decoded = reader.read(64, instance) && reader.read(8, biasedIndex);
    const int socket = static_cast<int>(biasedIndex) - 128;
    const char* reason = decoded ? "unsupported" : "parse";
    if (decoded && message.opcode == 801 && socket >= 0) {
        auto& mutation = outcome.mutation.emplace<state::PendingAbilitySelection>();
        if (state::prepare_ability_selection(instance, static_cast<std::uint8_t>(socket), mutation)) {
            reason = "ready";
        } else {
            outcome.mutation.emplace<std::monostate>();
            reason = "state";
        }
    }
    const bool ready = std::string_view(reason) == "ready";
    std::array<char, 192> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws%u stage=prepare result=%s reason=%s instance=0x%llX socket=%d payload_bytes=%zu",
        static_cast<unsigned>(message.opcode),
        ready ? "ok" : "fail",
        reason,
        static_cast<unsigned long long>(instance),
        socket,
        message.payload.size());
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         ready ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

void transfer_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    opcode405::Request request{};
    if (!opcode405::parse_request(message, request)) {
        report_transfer(message, request, "fail", "parse", "unknown");
        return;
    }
    auto& mutation = outcome.mutation.emplace<state::PendingVaultTransfer>();
    if (request.quantity != 1 || !state::prepare_vault_transfer(request.itemInstanceSoid, mutation)) {
        outcome.mutation.emplace<std::monostate>();
        report_transfer(message, request, "fail", "state", "unknown");
        return;
    }
    state::build_data::items::Definition definition{};
    if (request.definitionIndex < 0
        || !state::build_data::find_item_definition_index(
            static_cast<std::uint16_t>(request.definitionIndex), definition)
        || definition.definitionHash != mutation.movedItem.definitionHash) {
        const char* direction = mutation.toVault ? "to_vault" : "from_vault";
        outcome.mutation.emplace<std::monostate>();
        report_transfer(message, request, "fail", "identity", direction);
        return;
    }
    report_transfer(message, request, "ok", "ready", mutation.toVault ? "to_vault" : "from_vault");
}

} // namespace dawn::server::web_service
