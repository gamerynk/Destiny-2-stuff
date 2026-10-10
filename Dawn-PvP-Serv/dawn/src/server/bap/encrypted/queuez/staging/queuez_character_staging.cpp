#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../queuez_state_validation.h"

namespace dawn::server::bap::encrypted::queuez {

bool stage_change_character(const SessionState& before, ChangeCharacter& change) noexcept {
    change = {};
    if (!valid(before) || !before.family4Active || !before.family3Active
        || before.family4RootSoid == 0 || before.family4ResidentCount == 0
        || before.family4ResidentCount > before.family4Residents.size()
        || before.family3Phase != Family3Phase::normal) {
        return false;
    }
    const ResidentObject& account = before.family4Residents.front();
    if (account.definitionId == 0 || account.objectSoid != before.family4RootSoid) {
        return false;
    }
    change.after = before;
    change.after.family4Version = before.family4Version + 1;
    change.after.family3Phase = Family3Phase::publishOnce;
    change.accountDefinitionId = account.definitionId;
    return true;
}

bool stage_select_character(const SessionState& before,
                            std::uint64_t selectedCharacterSoid,
                            SelectCharacter& select) noexcept {
    select = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t characterDefinitionId = 0;
    if (!valid(before) || !before.family4Active || selectedCharacterSoid == 0
        || before.family4RootSoid == 0 || selectedCharacterSoid == before.family4RootSoid
        || before.family4ResidentCount == 0
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)) {
        return false;
    }
    const ResidentObject& account = before.family4Residents.front();
    if (account.definitionId != accountDefinitionId
        || account.objectSoid != before.family4RootSoid) {
        return false;
    }
    // the character object is the only resident a selection owns; its slot is found by definition,
    // not position, which is what lets the first pick add it
    std::size_t characterIndex = before.family4Residents.size();
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        if (before.family4Residents[index].definitionId == characterDefinitionId) {
            characterIndex = index;
            break;
        }
    }
    const bool seated = characterIndex < before.family4Residents.size();
    if (seated && before.family4Residents[characterIndex].objectSoid == 0) {
        return false;
    }
    // a pick naming the resident character is still the selection boundary; the client builds its
    // fireteam presentation after it, so suppressing the upsert leaves empty defaults. Upsert in place
    const bool changePending = before.family3Phase != Family3Phase::normal;
    const bool samePick =
        seated && before.family4Residents[characterIndex].objectSoid == selectedCharacterSoid;
    for (std::size_t index = 0; !samePick && index < before.family4ResidentCount; ++index) {
        if (before.family4Residents[index].objectSoid == selectedCharacterSoid) {
            return false;
        }
    }

    select.after = before;
    select.after.family4Version = before.family4Version + 1;
    if (!seated) {
        if (before.family4ResidentCount >= before.family4Residents.size()) {
            return false;
        }
        characterIndex = before.family4ResidentCount;
        select.after.family4ResidentCount = before.family4ResidentCount + 1;
        select.after.family4Residents[characterIndex].definitionId = characterDefinitionId;
    }
    select.after.family4Residents[characterIndex].objectSoid = selectedCharacterSoid;
    select.after.family3Phase = Family3Phase::normal;
    select.accountDefinitionId = accountDefinitionId;
    select.characterDefinitionId = characterDefinitionId;
    select.previousCharacterSoid = seated ? before.family4Residents[characterIndex].objectSoid : 0;
    select.selectedCharacterSoid = selectedCharacterSoid;
    select.patchAccount = changePending;
    return valid(select.after);
}

bool stage_equipment_swap(const SessionState& before,
                          std::uint64_t characterSoid,
                          EquipmentSwap& swap) noexcept {
    swap = {};
    std::uint32_t characterDefinitionId = 0;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0 || characterSoid == 0
        || before.family4ResidentCount == 0
        || before.family4ResidentCount > before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)) {
        return false;
    }
    bool resident = false;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const ResidentObject& object = before.family4Residents[index];
        if (object.definitionId == characterDefinitionId && object.objectSoid == characterSoid) {
            resident = true;
            break;
        }
    }
    if (!resident) {
        return false;
    }
    swap.after = before;
    ++swap.after.family4Version;
    swap.characterDefinitionId = characterDefinitionId;
    swap.characterSoid = characterSoid;
    const bool staged = valid(swap.after);
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=equip stage=queuez_version result=%s root=0x%llX before=%d after=%d residents=%u "
        "character=0x%llX definition=%u",
        staged ? "ok" : "fail",
        static_cast<unsigned long long>(before.family4RootSoid),
        before.family4Version,
        swap.after.family4Version,
        static_cast<unsigned>(before.family4ResidentCount),
        static_cast<unsigned long long>(characterSoid),
        characterDefinitionId);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_character_appearance_refresh(const SessionState& before,
                                        std::uint64_t characterSoid,
                                        CharacterAppearanceRefresh& refresh) noexcept {
    refresh = {};
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0
        || !before.family0Active || characterSoid == 0 || before.family0Character != characterSoid
        || before.family0Version == (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    refresh.after = before;
    ++refresh.after.family0Version;
    refresh.characterSoid = characterSoid;
    const bool staged = valid(refresh.after);
    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=equip stage=family0_version result=%s root=0x%llX before=%d after=%d "
                      "character=0x%llX",
                      staged ? "ok" : "fail",
                      static_cast<unsigned long long>(before.family4RootSoid),
                      before.family0Version,
                      refresh.after.family0Version,
                      static_cast<unsigned long long>(characterSoid));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_roster_appearance_refresh(const SessionState& before,
                                     std::uint64_t characterSoid,
                                     bool includeRoster,
                                     RosterAppearanceRefresh& refresh) noexcept {
    refresh = {};
    if (!valid(before) || !before.family3Active || before.family3RootSoid == 0 || characterSoid == 0
        || before.family3Version == (std::numeric_limits<std::int32_t>::max)()
        || (before.family4Active && before.family4RootSoid != before.family3RootSoid)) {
        return false;
    }
    refresh.after = before;
    ++refresh.after.family3Version;
    refresh.characterSoid = characterSoid;
    refresh.includeRoster = includeRoster;
    const bool staged = valid(refresh.after);
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=appearance stage=family3_version result=%s root=0x%llX before=%d after=%d "
        "character=0x%llX roster=%u",
        staged ? "ok" : "fail",
        static_cast<unsigned long long>(before.family3RootSoid),
        before.family3Version,
        refresh.after.family3Version,
        static_cast<unsigned long long>(characterSoid),
        includeRoster ? 1U : 0U);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_socket_plug(const SessionState& before,
                       std::uint64_t accountSoid,
                       std::uint64_t characterSoid,
                       std::uint64_t targetInstanceSoid,
                       bool updatesAccount,
                       SocketPlug& socketPlug) noexcept {
    socketPlug = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t characterDefinitionId = 0;
    std::uint32_t itemInstanceDefinitionId = 0;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0 || accountSoid == 0
        || accountSoid != before.family4RootSoid || characterSoid == 0 || targetInstanceSoid == 0
        || characterSoid == targetInstanceSoid || before.family4ResidentCount == 0
        || before.family4ResidentCount > before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kItemInstanceSlot, itemInstanceDefinitionId)) {
        return false;
    }

    std::size_t accountMatches = 0;
    std::size_t characterMatches = 0;
    std::size_t targetMatches = 0;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const ResidentObject& object = before.family4Residents[index];
        accountMatches += static_cast<std::size_t>(object.objectSoid == accountSoid
                                                   && object.definitionId == accountDefinitionId);
        characterMatches += static_cast<std::size_t>(
            object.objectSoid == characterSoid && object.definitionId == characterDefinitionId);
        targetMatches +=
            static_cast<std::size_t>(object.objectSoid == targetInstanceSoid
                                     && object.definitionId == itemInstanceDefinitionId);
    }
    if (accountMatches != 1 || characterMatches != 1 || targetMatches != 1) {
        return false;
    }

    socketPlug.after = before;
    ++socketPlug.after.family4Version;
    socketPlug.accountDefinitionId = accountDefinitionId;
    socketPlug.itemInstanceDefinitionId = itemInstanceDefinitionId;
    socketPlug.accountSoid = accountSoid;
    socketPlug.characterSoid = characterSoid;
    socketPlug.targetInstanceSoid = targetInstanceSoid;
    socketPlug.updatesAccount = updatesAccount;
    const bool staged = valid(socketPlug.after);
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=socket_plug stage=queuez_version result=%s root=0x%llX before=%d after=%d "
        "residents=%u account=0x%llX character=0x%llX instance=0x%llX "
        "item_definition=%u account_update=%u",
        staged ? "ok" : "fail",
        static_cast<unsigned long long>(before.family4RootSoid),
        before.family4Version,
        socketPlug.after.family4Version,
        static_cast<unsigned>(before.family4ResidentCount),
        static_cast<unsigned long long>(accountSoid),
        static_cast<unsigned long long>(characterSoid),
        static_cast<unsigned long long>(targetInstanceSoid),
        itemInstanceDefinitionId,
        static_cast<unsigned>(updatesAccount));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_item_acquisition(const SessionState& before,
                            std::uint64_t accountSoid,
                            std::uint64_t characterSoid,
                            std::uint64_t acquiredInstanceSoid,
                            bool updatesAccount,
                            ItemAcquisition& acquisition) noexcept {
    acquisition = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t characterDefinitionId = 0;
    std::uint32_t itemInstanceDefinitionId = 0;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0 || accountSoid == 0
        || accountSoid != before.family4RootSoid || characterSoid == 0 || acquiredInstanceSoid == 0
        || before.family4ResidentCount == 0
        || before.family4ResidentCount >= before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kItemInstanceSlot, itemInstanceDefinitionId)) {
        return false;
    }

    bool accountResident = false;
    bool characterResident = false;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const ResidentObject& object = before.family4Residents[index];
        if (object.objectSoid == acquiredInstanceSoid) {
            return false;
        }
        accountResident =
            accountResident
            || (object.definitionId == accountDefinitionId && object.objectSoid == accountSoid);
        characterResident =
            characterResident
            || (object.definitionId == characterDefinitionId && object.objectSoid == characterSoid);
    }
    if (!accountResident || !characterResident) {
        return false;
    }

    acquisition.after = before;
    ++acquisition.after.family4Version;
    acquisition.after.family4Residents[before.family4ResidentCount] =
        ResidentObject{acquiredInstanceSoid, itemInstanceDefinitionId};
    ++acquisition.after.family4ResidentCount;
    acquisition.accountDefinitionId = accountDefinitionId;
    acquisition.characterDefinitionId = characterDefinitionId;
    acquisition.itemInstanceDefinitionId = itemInstanceDefinitionId;
    acquisition.accountSoid = accountSoid;
    acquisition.characterSoid = characterSoid;
    acquisition.acquiredInstanceSoid = acquiredInstanceSoid;
    acquisition.updatesAccount = updatesAccount;
    const bool staged = valid(acquisition.after);

    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=acquire stage=queuez_version result=%s root=0x%llX before=%d after=%d "
                      "residents_before=%u residents_after=%u character=0x%llX instance=0x%llX "
                      "character_definition=%u item_definition=%u account_update=%u",
                      staged ? "ok" : "fail",
                      static_cast<unsigned long long>(before.family4RootSoid),
                      before.family4Version,
                      acquisition.after.family4Version,
                      static_cast<unsigned>(before.family4ResidentCount),
                      static_cast<unsigned>(acquisition.after.family4ResidentCount),
                      static_cast<unsigned long long>(characterSoid),
                      static_cast<unsigned long long>(acquiredInstanceSoid),
                      characterDefinitionId,
                      itemInstanceDefinitionId,
                      static_cast<unsigned>(updatesAccount));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_profile_item_acquisition(const SessionState& before,
                                    std::uint64_t accountSoid,
                                    std::uint64_t acquiredInstanceSoid,
                                    bool actionSource,
                                    bool appended,
                                    ProfileItemAcquisition& acquisition) noexcept {
    acquisition = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t itemInstanceDefinitionId = 0;
    if (!valid(before) || actionSource != (acquiredInstanceSoid != 0) || !before.family4Active
        || before.family4RootSoid == 0 || accountSoid == 0 || accountSoid != before.family4RootSoid
        || before.family4ResidentCount == 0
        || before.family4ResidentCount > before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)) {
        return false;
    }
    const ResidentObject& account = before.family4Residents.front();
    if (account.objectSoid != accountSoid || account.definitionId != accountDefinitionId) {
        return false;
    }

    std::size_t profileResidentMatches = 0;
    if (actionSource) {
        if (!middleware::datagen::object_id(kAccountFamilyType,
                                            middleware::datagen::kItemInstanceSlot,
                                            itemInstanceDefinitionId)) {
            return false;
        }
        for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
            const ResidentObject& resident = before.family4Residents[index];
            if (resident.objectSoid != acquiredInstanceSoid) {
                continue;
            }
            if (resident.definitionId != itemInstanceDefinitionId) {
                return false;
            }
            ++profileResidentMatches;
        }
    }

    const bool appendResident = appended && actionSource;
    if ((appendResident
         && (profileResidentMatches != 0
             || before.family4ResidentCount >= before.family4Residents.size()))
        || (!appendResident && acquiredInstanceSoid != 0 && profileResidentMatches != 1)) {
        return false;
    }

    acquisition.after = before;
    ++acquisition.after.family4Version;
    if (appendResident) {
        acquisition.after.family4Residents[before.family4ResidentCount] =
            ResidentObject{acquiredInstanceSoid, itemInstanceDefinitionId};
        ++acquisition.after.family4ResidentCount;
    }
    acquisition.accountDefinitionId = accountDefinitionId;
    acquisition.itemInstanceDefinitionId = itemInstanceDefinitionId;
    acquisition.accountSoid = accountSoid;
    acquisition.acquiredInstanceSoid = acquiredInstanceSoid;
    acquisition.actionSource = actionSource;
    acquisition.appendedResident = appendResident;
    const bool staged = valid(acquisition.after);
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=profile_acquire stage=queuez_version result=%s root=0x%llX before=%d after=%d "
        "residents_before=%u residents_after=%u account_definition=%u "
        "instance=0x%llX item_definition=%u action_source=%u appended_row=%u "
        "appended_resident=%u",
        staged ? "ok" : "fail",
        static_cast<unsigned long long>(before.family4RootSoid),
        before.family4Version,
        acquisition.after.family4Version,
        static_cast<unsigned>(before.family4ResidentCount),
        static_cast<unsigned>(acquisition.after.family4ResidentCount),
        accountDefinitionId,
        static_cast<unsigned long long>(acquiredInstanceSoid),
        itemInstanceDefinitionId,
        static_cast<unsigned>(actionSource),
        static_cast<unsigned>(appended),
        static_cast<unsigned>(appendResident));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_item_dismantle(const SessionState& before,
                          std::uint64_t accountSoid,
                          std::uint64_t characterSoid,
                          std::uint64_t dismantledInstanceSoid,
                          bool updatesAccount,
                          ItemDismantle& dismantle) noexcept {
    dismantle = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t characterDefinitionId = 0;
    std::uint32_t itemInstanceDefinitionId = 0;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0 || accountSoid == 0
        || accountSoid != before.family4RootSoid || characterSoid == 0
        || dismantledInstanceSoid == 0 || before.family4ResidentCount == 0
        || before.family4ResidentCount > before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kItemInstanceSlot, itemInstanceDefinitionId)) {
        return false;
    }

    bool accountResident = false;
    bool characterResident = false;
    std::size_t dismantledResidentIndex = before.family4Residents.size();
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const ResidentObject& object = before.family4Residents[index];
        accountResident =
            accountResident
            || (object.definitionId == accountDefinitionId && object.objectSoid == accountSoid);
        characterResident =
            characterResident
            || (object.definitionId == characterDefinitionId && object.objectSoid == characterSoid);
        if (object.objectSoid != dismantledInstanceSoid) {
            continue;
        }
        if (object.definitionId != itemInstanceDefinitionId
            || dismantledResidentIndex != before.family4Residents.size()) {
            return false;
        }
        dismantledResidentIndex = index;
    }
    if (!accountResident || !characterResident
        || dismantledResidentIndex >= before.family4ResidentCount) {
        return false;
    }

    dismantle.after = before;
    ++dismantle.after.family4Version;
    for (std::size_t index = dismantledResidentIndex + 1U; index < before.family4ResidentCount;
         ++index) {
        dismantle.after.family4Residents[index - 1U] = before.family4Residents[index];
    }
    --dismantle.after.family4ResidentCount;
    dismantle.after.family4Residents[dismantle.after.family4ResidentCount] = {};
    dismantle.accountDefinitionId = accountDefinitionId;
    dismantle.characterDefinitionId = characterDefinitionId;
    dismantle.itemInstanceDefinitionId = itemInstanceDefinitionId;
    dismantle.accountSoid = accountSoid;
    dismantle.characterSoid = characterSoid;
    dismantle.dismantledInstanceSoid = dismantledInstanceSoid;
    dismantle.updatesAccount = updatesAccount;
    const bool staged = valid(dismantle.after);

    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=dismantle stage=queuez_version result=%s root=0x%llX before=%d after=%d "
                      "residents_before=%u residents_after=%u resident_index=%zu character=0x%llX "
                      "instance=0x%llX account_definition=%u character_definition=%u "
                      "item_definition=%u account_update=%u",
                      staged ? "ok" : "fail",
                      static_cast<unsigned long long>(before.family4RootSoid),
                      before.family4Version,
                      dismantle.after.family4Version,
                      static_cast<unsigned>(before.family4ResidentCount),
                      static_cast<unsigned>(dismantle.after.family4ResidentCount),
                      dismantledResidentIndex,
                      static_cast<unsigned long long>(characterSoid),
                      static_cast<unsigned long long>(dismantledInstanceSoid),
                      accountDefinitionId,
                      characterDefinitionId,
                      itemInstanceDefinitionId,
                      static_cast<unsigned>(updatesAccount));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return staged;
}

bool stage_vault_transfer(const SessionState& before,
                          std::uint64_t accountSoid,
                          std::uint64_t characterSoid,
                          std::uint64_t instanceSoid,
                          VaultTransfer& transfer) noexcept {
    transfer = {};
    std::uint32_t accountDefinitionId = 0;
    std::uint32_t characterDefinitionId = 0;
    std::uint32_t itemInstanceDefinitionId = 0;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0 || accountSoid == 0
        || accountSoid != before.family4RootSoid || characterSoid == 0 || instanceSoid == 0
        || before.family4ResidentCount == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kAccountSlot, accountDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kCharacterSlot, characterDefinitionId)
        || !middleware::datagen::object_id(
            kAccountFamilyType, middleware::datagen::kItemInstanceSlot, itemInstanceDefinitionId)) {
        return false;
    }
    std::size_t accountMatches = 0;
    std::size_t characterMatches = 0;
    std::size_t instanceMatches = 0;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const ResidentObject& object = before.family4Residents[index];
        accountMatches += static_cast<std::size_t>(object.objectSoid == accountSoid
                                                   && object.definitionId == accountDefinitionId);
        characterMatches += static_cast<std::size_t>(
            object.objectSoid == characterSoid && object.definitionId == characterDefinitionId);
        instanceMatches += static_cast<std::size_t>(
            object.objectSoid == instanceSoid && object.definitionId == itemInstanceDefinitionId);
    }
    transfer.after = before;
    ++transfer.after.family4Version;
    transfer.accountDefinitionId = accountDefinitionId;
    transfer.characterDefinitionId = characterDefinitionId;
    transfer.itemInstanceDefinitionId = itemInstanceDefinitionId;
    transfer.accountSoid = accountSoid;
    transfer.characterSoid = characterSoid;
    transfer.instanceSoid = instanceSoid;
    const bool staged = accountMatches == 1 && characterMatches == 1 && instanceMatches == 1
                        && valid(transfer.after);

    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=vault stage=queuez_version result=%s root=0x%llX before=%d after=%d residents=%u "
        "account_matches=%zu character_matches=%zu instance_matches=%zu instance=0x%llX",
        staged ? "ok" : "fail",
        static_cast<unsigned long long>(before.family4RootSoid),
        before.family4Version,
        transfer.after.family4Version,
        static_cast<unsigned>(before.family4ResidentCount),
        accountMatches,
        characterMatches,
        instanceMatches,
        static_cast<unsigned long long>(instanceSoid));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         staged ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    if (!staged) {
        transfer = {};
    }
    return staged;
}

} // namespace dawn::server::bap::encrypted::queuez
