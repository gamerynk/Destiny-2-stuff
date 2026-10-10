#include "../content/content_catalog.h"
#include "runtime.h"
#include "runtime/domain_markers.h"
#include "runtime/persistence/publication_transaction.h"

namespace dawn::state::build_data {

namespace {

void rollback_named_publication() noexcept {
    runtime::named::clear();
    content::clear();
}

} // namespace

bool named_catalog_ready() noexcept {
    return runtime::named::ready();
}

bool publish_named_catalog() noexcept {
    runtime::persistence::Transaction transaction;
    if (!transaction.active() || !content::seal()) {
        return false;
    }
    runtime::named::publish();
    return transaction.finish(true, rollback_named_publication);
}

} // namespace dawn::state::build_data
