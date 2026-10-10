#include <Windows.h>

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../items/packages/build.h"
#include "internal.h"
#include "runtime.h"

namespace dawn::client::content::investment {
namespace {

SRWLOCK g_refreshLock{SRWLOCK_INIT};

// destination layouts and spawn sets must stay in this test: this is the only caller of the
// package pass, so leaving a domain out stops its extraction and the cache is never written
[[nodiscard]] bool ready() noexcept {
    return state::build_data::named_catalog_ready() && state::build_data::item_definitions_ready()
           && state::build_data::collectible_definitions_ready()
           && state::build_data::material_requirement_sets_ready()
           && state::build_data::configured_item_details_ready()
           && state::build_data::socket_plug_rules_ready()
           && state::build_data::inventory_bucket_descriptors_ready()
           && state::build_data::socket_entry_lists_ready()
           && state::build_data::ability_buckets_ready()
           && state::build_data::progression_definitions_ready()
           && state::build_data::scenario_layouts_ready() && state::build_data::spawn_sets_ready()
           && state::build_data::hash_names_ready()
           && state::build_data::investment_constants_ready();
}

[[nodiscard]] bool emote_collection_settled() noexcept {
    return state::ensure_character_emote_collection() != state::EmoteCollectionOutcome::failed;
}

} // namespace

bool refresh() noexcept {
    if (ready()) {
        // same lock as the extraction path; a cache write holds its own lock across file calls
        AcquireSRWLockExclusive(&g_refreshLock);
        const bool persisted = state::ensure_profile_item_identities()
                               && emote_collection_settled() && state::build_data::persist();
        middleware::content::packages::reader::release_caches();
        ReleaseSRWLockExclusive(&g_refreshLock);
        return persisted;
    }

    AcquireSRWLockExclusive(&g_refreshLock);
    (void)items::packages::build();
    const bool domainsReady = ready();
    const bool complete = domainsReady && state::ensure_profile_item_identities()
                          && emote_collection_settled() && state::build_data::persist();
    ReleaseSRWLockExclusive(&g_refreshLock);
    return complete;
}

} // namespace dawn::client::content::investment
