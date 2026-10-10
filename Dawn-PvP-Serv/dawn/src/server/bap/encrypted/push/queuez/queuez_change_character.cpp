#include <array>

#include "../../../../../middleware/datagen/family4/account/selection_patch/account_selection_patch_encoder.h"
#include "../../queuez/queuez_state_validation.h"
#include "queuez_update_frame.h"

namespace dawn::server::bap::encrypted::push {

namespace selection_patch = middleware::datagen::family4::account::selection_patch;

bool append_change_character_notification(Scratch& scratch,
                                          const queuez::ChangeCharacter& change,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::span<const std::byte, state::kBapNonceSize> nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written) noexcept {
    std::size_t patchSize = 0;
    if (!selection_patch::encode(0, scratch.plaintext, patchSize)
        || patchSize != selection_patch::kPayloadSize) {
        queuez_frame::clear_object_storage(scratch, patchSize, 0);
        return false;
    }
    const middleware::queuez::Object object{
        change.accountDefinitionId,
        change.after.family4RootSoid,
        middleware::queuez::Encoding::tagReflection,
        std::span(scratch.plaintext).first(patchSize),
    };
    const std::array objects{object};
    const middleware::queuez::Family family{
        queuez::kAccountFamilyType,
        change.after.family4RootSoid,
        change.after.family4Version,
        0,
        objects,
    };
    return queuez_frame::append(scratch, family, patchSize, 0, key, nonce, response, written);
}

} // namespace dawn::server::bap::encrypted::push
