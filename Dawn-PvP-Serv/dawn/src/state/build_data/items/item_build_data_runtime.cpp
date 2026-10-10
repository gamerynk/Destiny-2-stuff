#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"

namespace dawn::state::build_data {

bool item_definitions_ready() noexcept {
    return items::count() != 0;
}

bool publish_item_definitions(std::span<const items::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && transaction.finish(items::replace(definitions), items::clear);
}

bool find_item_definition(std::uint32_t definitionHash,
                          std::uint8_t bucketId,
                          items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find(definitionHash, bucketId, definition);
}

bool find_item_definition_hash(std::uint32_t definitionHash,
                               items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find_hash(definitionHash, definition);
}

bool find_item_definition_index(std::uint16_t definitionIndex,
                                items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find_index(definitionIndex, definition)
           && definition.definitionIndex == definitionIndex;
}

} // namespace dawn::state::build_data
