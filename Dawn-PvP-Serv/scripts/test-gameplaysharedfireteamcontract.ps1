$ErrorActionPreference = 'Stop'

$groupPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\gameplay\group\group_host.cpp')
$peerPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\gameplay\peer\peer_transport.cpp')
$realtimePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\steam\runtime\social\realtime_client.cpp')
$matchmakingPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\steam\interfaces\methods\matchmaking.cpp')
$bootstrapPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\client\hooks\bootflow\dedicated_fireteam_bootstrap.cpp')
$activityKeepalivePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\push\activity\activity_keepalive_push.cpp')
$activityRosterSnapshotPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\push\activity\activity_roster_snapshot.cpp')
$activityRosterGenerationPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\push\activity\activity_roster_generation_policy.h')
$activityNotificationsPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\activity_transaction\activity_transaction_notifications.cpp')
$activityPublicationPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\bap_connection_publication.cpp')
$activityRoutePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\activity_message\activity_message_route.cpp')
$activityMembershipPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\activity_message\membership\activity_membership_route.cpp')
$bapInternalPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\internal.h')
$membershipPolicyPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\push\activity\activity_membership_publication_policy.h')
$hostSessionsPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\gameplay\group\group_host_sessions.cpp')
$activitySeedPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\push\activity\activity_keepalive_seed.cpp')
$group = Get-Content -LiteralPath $groupPath -Raw
$peer = Get-Content -LiteralPath $peerPath -Raw
$realtime = Get-Content -LiteralPath $realtimePath -Raw
$matchmaking = Get-Content -LiteralPath $matchmakingPath -Raw
$bootstrap = Get-Content -LiteralPath $bootstrapPath -Raw
$activityKeepalive = Get-Content -LiteralPath $activityKeepalivePath -Raw
$activityRosterSnapshot = Get-Content -LiteralPath $activityRosterSnapshotPath -Raw
$activityRosterGeneration = Get-Content -LiteralPath $activityRosterGenerationPath -Raw
$activityNotifications = Get-Content -LiteralPath $activityNotificationsPath -Raw
$activityPublication = Get-Content -LiteralPath $activityPublicationPath -Raw
$activityRoute = Get-Content -LiteralPath $activityRoutePath -Raw
$activityMembership = Get-Content -LiteralPath $activityMembershipPath -Raw
$bapInternal = Get-Content -LiteralPath $bapInternalPath -Raw
$membershipPolicy = Get-Content -LiteralPath $membershipPolicyPath -Raw
$hostSessions = Get-Content -LiteralPath $hostSessionsPath -Raw
$activitySeed = Get-Content -LiteralPath $activitySeedPath -Raw

if ($group -match 'g_membershipRevision' `
    -or $group -notmatch 'std::uint32_t membershipRevision\{\}' `
    -or $group -notmatch 'update\.revision\s*=\s*\+\+additionState->membershipRevision\s*;') {
    throw 'Gameplay membership revisions must restart at one for each native group session.'
}

$publicTargetStart = $activityKeepalive.IndexOf('if (session.activityJoinedForeignSession)')
$publicTargetEnd = $activityKeepalive.IndexOf(
    'if constexpr (!core::runtime::policy::kActivityBackendReplication)',
    $publicTargetStart)
if ($publicTargetStart -lt 0 -or $publicTargetEnd -le $publicTargetStart) {
    throw 'Could not isolate the public-target activity keepalive.'
}
$publicTarget = $activityKeepalive.Substring(
    $publicTargetStart, $publicTargetEnd - $publicTargetStart)
if ($hostSessions -notmatch 'source_activity_session_for_host\(std::uint64_t hostSessionId\)' `
    -or $hostSessions -notmatch 'region_for_host_session\(std::uint64_t hostSessionId\)' `
    -or $publicTarget -notmatch 'source_activity_session_for_host\(' `
    -or $publicTarget -notmatch 'region_for_host_session\(session\.activitySessionId\)' `
    -or $publicTarget -notmatch 'reported_region\(session\.activitySessionId\)' `
    -or $publicTarget -notmatch 'prepare_refresh\(\s*session\.activitySessionId' `
    -or $publicTarget -notmatch 'append_membership_notification\(' `
    -or $publicTarget -notmatch 'append_roster_notification\(' `
    -or $publicTarget -notmatch 'activityContract\.ready_for_public_target_roster\(assignedRegionReady\)' `
    -or $publicTarget -notmatch 'public_membership_settled\(') {
    throw 'PUBLIC TARGET must use its assigned region and publish its own native membership/roster pair.'
}
if ($publicTarget -match 'live_region_session\(' `
    -or $publicTarget -match 'prepare_refresh\(\s*sourceSessionId') {
    throw 'PUBLIC TARGET must not guess by recency or mirror the private parent membership snapshot.'
}
$publicReadyGate = $publicTarget.IndexOf('if (!rosterReady)')
$publicGlobalState = $publicTarget.IndexOf('published = append_global_state_notification(')
if ($publicReadyGate -lt 0 -or $publicGlobalState -le $publicReadyGate) {
    throw 'PUBLIC TARGET must settle its accepted join before periodic global-state replay.'
}
if ($activityPublication -notmatch 'region_for_host_session\(session\.activitySessionId\)' `
    -or $activityPublication -notmatch 'seed_transition_region\(\s*session\.activitySessionId,\s*publicTargetRegion\)' `
    -or $activitySeed -notmatch 'seed_transition_region\(std::uint64_t sessionId,\s*std::int32_t regionIndex\)') {
    throw 'A public host must seed the exact region assigned by its retained host-session row.'
}
if ($membershipPolicy -notmatch 'kPublicMembershipSettleDelayMs\s*=\s*300' `
    -or $membershipPolicy -notmatch 'membership_phase_sends_snapshot' `
    -or $publicTarget -notmatch 'activityPublicMembershipNotBeforeTick') {
    throw 'PUBLIC TARGET membership must retain the proven settlement and unspent-revision ordering.'
}
if ($activityNotifications -notmatch 'ready_for_public_target_roster\(' `
    -or $activityNotifications -notmatch 'assigned_public_region_ready\(' `
    -or $activityNotifications -notmatch 'defer_to_keepalive\(') {
    throw 'Transaction publication must preserve the public child setup and two-stage membership contract.'
}
if ($activityNotifications -notmatch 'if \(activity\.regionMoved\)\s*\{[\s\S]*?append_roster_notification\(') {
    throw 'A native region move must return its authoritative roster before the settled keepalive publication.'
}
if ($activityRosterGeneration -notmatch 'kWarmupSends\s*=\s*3' `
    -or $activityRosterGeneration -notmatch 'clientSyncKnown\s*&&\s*previousClientSync\s*!=\s*nextClientSync' `
    -or $activityRosterGeneration -notmatch '!burst\s*&&\s*previousGroups\s*!=\s*nextGroups' `
    -or $activityRosterSnapshot -notmatch 'roster_generation::should_advance\(') {
    throw 'Roster loading must retain three bounded warm-up generations and advance once for a material client-sync edge.'
}
if ($activityPublication -notmatch 'activityForeignMembershipPublished' `
    -or $activityPublication -notmatch 'activityForeignRosterPublished' `
    -or $activityPublication -notmatch 'ready_for_public_target_roster\(' `
    -or $activityPublication -notmatch 'activityKeepaliveDueTick\s*=\s*GetTickCount64\(\)') {
    throw 'Completing PUBLIC TARGET setup must immediately re-arm its held native publication.'
}
if ($activityRoute -notmatch 'source_activity_session_for_host\(parsed\.sessionId\)' `
    -or $activityRoute -notmatch 'membership::member_key\(sourceSessionId\)' `
    -or $activityRoute -notmatch 'inheritedMemberKey\s*!=\s*0\s*\?\s*inheritedMemberKey\s*:\s*parsed\.memberKey' `
    -or $activityRoute -notmatch 'prepare_join\(\s*parsed\.sessionId,\s*memberKey') {
    throw 'A derived PUBLIC activity join must retain its authenticated PRIVATE parent member key.'
}
if ($activityMembership -notmatch 'source_activity_session_for_host\(request\.accountHandle\)' `
    -or $activityMembership -notmatch 'membership_state::member_key\(request\.accountHandle\)' `
    -or $activityMembership -notmatch 'make_identity\(parsed,\s*effectiveMemberKey\)') {
    throw 'A derived PUBLIC identity update must use the member key committed by its inherited join.'
}
$publicMembershipStart = $group.IndexOf(
    'bool publish_membership(const state::gameplay::Endpoint& peer,')
$publicMembershipEnd = $group.IndexOf('void service(std::uint64_t now)', $publicMembershipStart)
if ($publicMembershipStart -lt 0 -or $publicMembershipEnd -le $publicMembershipStart) {
    throw 'Could not isolate initial gameplay membership publication.'
}
$publicMembership = $group.Substring(
    $publicMembershipStart, $publicMembershipEnd - $publicMembershipStart)
if ($publicMembership -match 'seedInitialPlayer\s*=\s*!publicRegion' `
    -or $publicMembership -notmatch 'identityResolved\s*&&\s*!record->initialMembershipPublished' `
    -or $publicMembership -notmatch 'record->playerId\s*=\s*record->joiningPlatformId\s*;' `
    -or $publicMembership -notmatch 'record->membershipAdditionNumber\s*=\s*allocate_player_addition\(\*additions\)\s*;') {
    throw 'Every authenticated group, including a public Citizen, must publish its temporary player seed in initial membership.'
}

$joinParametersStart = $group.IndexOf(
    'bool publish_join_parameters(const state::gameplay::Endpoint& peer,')
$joinParametersEnd = $group.IndexOf('bool view_accepted(', $joinParametersStart)
if ($joinParametersStart -lt 0 -or $joinParametersEnd -le $joinParametersStart) {
    throw 'Could not isolate initial gameplay parameter publication.'
}
$joinParameters = $group.Substring(
    $joinParametersStart, $joinParametersEnd - $joinParametersStart)
if ($group -notmatch 'bool fill_current_activity\([\s\S]*source_activity_session\(groupSessionId\)[\s\S]*state::activity::destination::snapshot\(sourceActivitySessionId, destination\)[\s\S]*body\.sourceActivityIndex\s*=\s*destination\.previousActivityIndex\s*;[\s\S]*body\.activityIndex\s*=\s*destination\.activityIndex\s*;[\s\S]*body\.present\s*=\s*true\s*;') {
    throw 'The current-activity helper must preserve the selected destination in every full parameter publication.'
}
if ($group -notmatch 'void fill_previous_activity\([\s\S]*body\.present\s*=\s*true\s*;[\s\S]*body\.reason\s*=\s*0\s*;[\s\S]*body\.sourceActivityIndex\s*=\s*0\s*;[\s\S]*body\.activityIndex\s*=\s*0\s*;') {
    throw 'The previous-activity helper must initialize the native public-transition actual-activity-index.'
}
if ($joinParameters -notmatch 'const bool publicRegion\s*=\s*\r?\n?\s*source_activity_session\(sessionId\)\s*!=\s*state::activity::kAbsentSessionId\s*;' `
    -or $joinParameters -notmatch 'if \(publicRegion\)\s*\{[\s\S]*update\.releasedMask\s*=\s*goalDataBit\s*;' `
    -or $joinParameters -notmatch 'update\.carriedMask\s*=\s*currentActivityBit\s*\|\s*previousActivityBit\s*;' `
    -or $joinParameters -notmatch 'else\s*\{[\s\S]*update\.carriedMask\s*=\s*goalDataBit\s*\|\s*currentActivityBit\s*;' `
    -or $joinParameters -notmatch 'fill_current_activity\(update\.currentActivity, sessionId\)' `
    -or $joinParameters -notmatch 'if \(publicRegion\)\s*\{[\s\S]*fill_previous_activity\(update\.previousActivity\)\s*;') {
    throw 'Public Citizens must receive the released goal-data latch plus current/previous activity; central sessions retain Orbit values.'
}

$activityHostStart = $group.IndexOf('bool publish_activity_host_for_session(')
$activityHostEnd = $group.IndexOf('bool publish_peer_reestablish_membership(', $activityHostStart)
if ($activityHostStart -lt 0 -or $activityHostEnd -le $activityHostStart) {
    throw 'Could not isolate activity-host parameter publication.'
}
$activityHost = $group.Substring($activityHostStart, $activityHostEnd - $activityHostStart)
if ($activityHost -notmatch 'fill_current_activity\(update\.currentActivity, parameterSessionId\)' `
    -or $activityHost -notmatch 'fill_previous_activity\(update\.previousActivity\)' `
    -or $activityHost -match 'current-activity` carries an empty delta') {
    throw 'Public activity-host publication must repeat both selected current and initialized previous activity.'
}

$answerParametersStart = $group.IndexOf('void answer_parameters(')
$answerParametersEnd = $group.IndexOf('void answer_time(', $answerParametersStart)
if ($answerParametersStart -lt 0 -or $answerParametersEnd -le $answerParametersStart) {
    throw 'Could not isolate parameter-request answers.'
}
$answerParameters = $group.Substring(
    $answerParametersStart, $answerParametersEnd - $answerParametersStart)
if ($answerParameters -notmatch 'fill_current_activity\(currentActivity, sessionId\)' `
    -or $answerParameters -notmatch 'fill_previous_activity\(previousActivity\)' `
    -or $answerParameters -notmatch 'update\.currentActivity\s*=\s*currentActivity\s*;' `
    -or $answerParameters -notmatch 'update\.previousActivity\s*=\s*previousActivity\s*;') {
    throw 'Requested current/previous activity must receive the same full public-transition values.'
}

$memberMachineStart = $group.IndexOf(
    'std::uint64_t member_machine_id(const Admitted& record)')
$memberMachineEnd = $group.IndexOf('void member_self_address(', $memberMachineStart)
if ($memberMachineStart -lt 0 -or $memberMachineEnd -le $memberMachineStart) {
    throw 'Could not isolate managed-session member machine selection.'
}
$memberMachine = $group.Substring(
    $memberMachineStart, $memberMachineEnd - $memberMachineStart)
$publicActivityLookup = $memberMachine.IndexOf(
    'source_activity_session(record.sessionId)')
$inheritedMemberLookup = $memberMachine.IndexOf(
    'state::activity::membership::member_key(sourceActivitySessionId)')
$platformFallback = $memberMachine.IndexOf('record.joiningPlatformId != 0')
if ($publicActivityLookup -lt 0 -or $inheritedMemberLookup -le $publicActivityLookup `
    -or $platformFallback -le $inheritedMemberLookup `
    -or $memberMachine -notmatch 'activityMemberKey\s*!=\s*0[\s\S]*return activityMemberKey\s*;' `
    -or $memberMachine -notmatch 'return record\.joiningPlatformId\s*;') {
    throw 'Public-region managed membership must inherit the PRIVATE activity machine key before the central-fireteam platform fallback.'
}

$snapshotStart = $group.IndexOf('bool publish_snapshot(const Admitted& record)')
$snapshotEnd = $group.IndexOf('bool publish_activity_host_for_session(', $snapshotStart)
if ($snapshotStart -lt 0 -or $snapshotEnd -le $snapshotStart) {
    throw 'Could not isolate publish_snapshot.'
}
$snapshot = $group.Substring($snapshotStart, $snapshotEnd - $snapshotStart)
if ($snapshot -notmatch 'const bool publishesPlayerProfiles\s*=\s*\r?\n?\s*source_activity_session\(record\.sessionId\)\s*==\s*state::activity::kAbsentSessionId\s*;' `
    -or $snapshot -notmatch 'if \(publishesPlayerProfiles\)\s*\{[\s\S]*fill_membership_profile\(candidate\.joiningPlatformId, player\)') {
    throw 'Public-region group players must remain profile-absent while central fireteams keep full cards.'
}
if ($snapshot -notmatch '!coalescedOwner\s*&&\s*publishesPlayerProfiles\s*&&\s*successionIndex\s*==\s*kHostMemberIndex' `
    -or $snapshot -notmatch 'member\.ownedPlayerCountPresent\s*=\s*publishesPlayerProfiles\s*;' `
    -or $snapshot -notmatch 'member\.ownsPlayerSlot\s*=\s*true\s*;\s*member\.playerSlot\s*=\s*player\.slot\s*;' `
    -or $snapshot -notmatch 'update\.playerAdditionNumberPresent\s*=\s*publishesPlayerProfiles\s*;') {
    throw 'Central sessions must retain successor, scalar ownership, and addition generation while public Citizens keep the archived field-10-only, tail-absent variant.'
}
if ($snapshot -notmatch 'logicalOwnerMember\.joinId\s*=\s*record\.joinId\s*;' `
    -or $snapshot -notmatch 'logicalOwnerMember\.joinId\s*=\s*admittedLobbyOwner->joinId\s*;') {
    throw 'Logical owner member must preserve the owner join generation.'
}
if ($snapshot -notmatch 'else\s+if\s*\(coalescedOwner\)\s*\{\s*(?://[^\r\n]*\r?\n\s*)*logicalOwnerMember\.joinId\s*=\s*record\.sessionId\s*;') {
    throw 'An endpointless central owner must use a distinct session-scoped member-row key.'
}
if ($snapshot -notmatch 'logicalOwnerMember\.connectionPresent\s*=\s*true\s*;' `
    -or $snapshot -notmatch 'member\.connectionPresent\s*=\s*&candidate\s*==\s*&record\s*;') {
    throw 'Dedicated membership must connect member zero and self only, never command client P2P.'
}
if ($snapshot -match '(?m)^\s*member\.connectionPresent\s*=\s*true\s*;') {
    throw 'Remote gameplay rows still command the native client-to-client observer channel.'
}
if ($snapshot -notmatch 'candidate\.membershipAdditionNumberPresent' `
    -or $snapshot -notmatch 'player\.addSequence\s*=\s*candidate\.membershipAdditionNumber\s*;' `
    -or $snapshot -notmatch 'admittedLobbyOwner->membershipAdditionNumberPresent' `
    -or $snapshot -notmatch 'owner\.addSequence\s*=\s*admittedLobbyOwner->membershipAdditionNumber\s*;') {
    throw 'Membership must preserve each host-allocated native player-add generation.'
}
if ($snapshot -notmatch 'coalescedOwner' `
    -or $snapshot -notmatch 'candidate\.joiningPlatformId\s*==\s*lobbyOwnerPlatformId' `
    -or $snapshot -notmatch 'continue\s*;') {
    throw 'Central owner must be coalesced onto logical member zero instead of appended as member one.'
}
if ($group -notmatch 'void member_address_for_recipient\(' `
    -or $snapshot -notmatch 'member_address_for_recipient\(candidate,\s*record,\s*member\.address\)') {
    throw 'Membership must preserve the self address while assigning remote rows unique keys.'
}
$addressStart = $group.IndexOf('void member_address_for_recipient(')
$addressEnd = $group.IndexOf('bool authenticated_player_projection_ready(', $addressStart)
if ($addressStart -lt 0 -or $addressEnd -le $addressStart) {
    throw 'Could not isolate recipient-relative managed-session address selection.'
}
$addressSelection = $group.Substring($addressStart, $addressEnd - $addressStart)
if ($addressSelection -notmatch 'if \(self\)' `
    -or $addressSelection -notmatch 'member_self_address\(candidate,\s*output\)' `
    -or $addressSelection -notmatch 'write_net_addr\(candidate\.endpoint\.address,\s*candidate\.endpoint\.port,\s*output\)') {
    throw 'Every recipient must retain its exact native self NetAddr while remote rows use server-observed endpoints.'
}

$collectStart = $group.IndexOf('void collect_session_peers(')
$collectEnd = $group.IndexOf('bool authoritative_member_index(', $collectStart)
if ($collectStart -lt 0 -or $collectEnd -le $collectStart) {
    throw 'Could not isolate shared-session peer collection.'
}
$collect = $group.Substring($collectStart, $collectEnd - $collectStart)
if ($group -notmatch 'bool authenticated_player_projection_ready\(const Admitted& record\)' `
    -or $collect -notmatch '!recipientIsLobbyOwner' `
    -or $collect -notmatch 'candidate\.joiningPlatformId\s*==\s*lobbyOwnerPlatformId' `
    -or $collect -notmatch '!authenticated_player_projection_ready\(candidate\)') {
    throw 'Observers must retain the synthetic owner until the admitted owner has authenticated ID34.'
}

$serviceStart = $group.IndexOf('void service(std::uint64_t now)')
if ($serviceStart -lt 0) {
    throw 'Could not isolate group service.'
}
$service = $group.Substring($serviceStart)
if ($service -match 'queue_owner_gameplay_admission' `
    -or $service -match 'centralOwnerAdmission') {
    throw 'Group service must not inject the disproven server-forced owner gameplay descriptor.'
}
$dirty = $service.IndexOf('if (record.membershipDirty)')
$activityHostBranch = $service.IndexOf('if (activityHostOwed)')
if ($dirty -lt 0 -or $activityHostBranch -lt 0 -or $dirty -gt $activityHostBranch) {
    throw 'Membership refresh must run before optional activity-host publication.'
}
if ($service -notmatch 'held_host_session\(record\.sessionId\)\s*!=\s*state::activity::kAbsentSessionId') {
    throw 'Plain Orbit must not owe an activity-host publication without a held host session.'
}
if ($service -notmatch '!record\.initialHostReestablishPublished[\s\S]*kInitialHostReestablishDelay' `
    -or $service -match 'opening_authored_override_active\(\)\s*&&\s*record\.activityHostPublished') {
    throw 'A native destination must receive baseline kind-22 activation without a destination-specific gate.'
}
$baselineActivation = $service.IndexOf('if (!record.initialHostReestablishPublished)')
if ($baselineActivation -lt 0) {
    throw 'Native activity-manager baseline activation is missing.'
}
$hostReestablishStart = $group.IndexOf(
    'bool publish_host_reestablish(const Admitted& record)')
$hostReestablishEnd = $group.IndexOf(
    "`nvoid bind_view(", $hostReestablishStart + 1)
if ($hostReestablishStart -lt 0 -or $hostReestablishEnd -le $hostReestablishStart) {
    throw 'Could not isolate baseline host-reestablish publication.'
}
$hostReestablish = $group.Substring(
    $hostReestablishStart, $hostReestablishEnd - $hostReestablishStart)
if ($group -notmatch 'bool populate_native_host_identity\(' `
    -or $group -notmatch 'realtime_store::lobby_for_member\(record\.joiningPlatformId,\s*lobby\)' `
    -or $group -notmatch 'realtime_store::native_fireteam_descriptor\(' `
    -or $group -notmatch 'kNativeIdentity128Offset' `
    -or $group -notmatch 'kNativeIdentity144Offset' `
    -or $hostReestablish -notmatch 'populate_native_host_identity\(' `
    -or $hostReestablish -notmatch 'reason=native_identity') {
    throw 'Dedicated baseline activation must preserve the uploaded owner-native identity blocks.'
}
if ($group -match 'copy_n\(nativeDescriptor\.begin\(\)\s*,\s*body\.address' `
    -or $hostReestablish -match 'body\.machineId\s*=\s*nativeMachineId') {
    throw 'Owner-native identity must not replace the dedicated host machine id or direct NetAddr.'
}
if ($group -match 'opening_authored_override_active' `
    -or $group -match 'mark_opening_host_ready' `
    -or $group -match 'publish_host_handoff' `
    -or $group -match 'publish_host_transition' `
    -or $group -match 'kHostHandoffDelay') {
    throw 'The obsolete synthetic migration ladder remains in the gameplay host.'
}
if ($group -notmatch 'bool session_player_replacement_pending\(std::uint64_t sessionId\)') {
    throw 'Shared sessions must expose the ID36-to-ID34 replacement transaction gate.'
}
$dirtyEnd = $service.IndexOf('if (activityHostOwed)', $dirty)
$dirtyBranch = $service.Substring($dirty, $dirtyEnd - $dirty)
if ($dirtyBranch -notmatch 'session_player_replacement_pending\(record\.sessionId\)') {
    throw 'Observer membership publication must pause during any source player replacement.'
}

$playerAddStart = $group.IndexOf('if (id == wire::kPlayerAddId)')
$playerRemoveStart = $group.IndexOf('if (id == wire::kPlayerRemoveId)', $playerAddStart)
$playerRemoveEnd = $group.IndexOf("`n    return false;", $playerRemoveStart)
if ($playerAddStart -lt 0 -or $playerRemoveStart -le $playerAddStart `
    -or $playerRemoveEnd -le $playerRemoveStart) {
    throw 'Could not isolate native player replacement handlers.'
}
$playerAdd = $group.Substring($playerAddStart, $playerRemoveStart - $playerAddStart)
$playerRemove = $group.Substring($playerRemoveStart, $playerRemoveEnd - $playerRemoveStart)
if ($playerRemove -notmatch 'record->playerReplacementPending\s*=\s*true\s*;' `
    -or $playerRemove -match 'mark_other_members_dirty') {
    throw 'ID36 must commit a source-only pending replacement without exposing players-1 to observers.'
}
$candidatePublish = $playerAdd.IndexOf('published = publish_snapshot(candidate)')
$commitReplacement = $playerAdd.IndexOf('record->playerReplacementPending = false')
$publishObservers = $playerAdd.IndexOf('mark_other_members_dirty(record->sessionId, record->endpoint)')
if ($playerAdd -notmatch 'Admitted candidate\s*=\s*\*record\s*;' `
    -or $candidatePublish -lt 0 -or $commitReplacement -le $candidatePublish `
    -or $publishObservers -le $commitReplacement) {
    throw 'ID34 must queue the final source snapshot, commit the replacement, then refresh observers.'
}
if ($playerAdd -notmatch 'candidate\.playerRequestSequence\s*=\s*request\.sequence\s*;' `
    -or $playerAdd -notmatch 'record->playerRequestSequence\s*=\s*request\.sequence\s*;' `
    -or $playerAdd -notmatch 'record->playerRequestSequence\s*==\s*request\.sequence') {
    throw 'ID34 must preserve its request sequence and include it in idempotence.'
}

$closedStart = $peer.IndexOf(
    'if (header.id == static_cast<std::uint8_t>(wire::ConnectId::closed))')
$closedEnd = $peer.IndexOf('if (group::consume(', $closedStart)
if ($closedStart -lt 0 -or $closedEnd -le $closedStart) {
    throw 'Could not isolate connect-closed handling.'
}
$closed = $peer.Substring($closedStart, $closedEnd - $closedStart)
if ($closed -notmatch 'group::release_endpoint\(from\)\s*;') {
    throw 'A gameplay close must release only the transport-owned gameplay rows.'
}
if ($closed -match 'release_endpoint\(from,\s*true\)' -or $closed -match 'remove_lobby_member') {
    throw 'A gameplay close must never mutate the independently authoritative realtime lobby.'
}

$bootIsHandled = $group -match 'if \(id == wire::kBootMachineId\)'
$bootIsOwnerAuthorized = $group -match 'source->joiningPlatformId == source->lobbyOwnerPlatformId'
$bootNotifiesTarget = $group -match 'wire::SessionMessageId::sessionBoot'
$bootMutatesLobby = $group -match 'remove_lobby_member\('
if (-not ($bootIsHandled -and $bootIsOwnerAuthorized -and $bootNotifiesTarget -and $bootMutatesLobby)) {
    throw 'Authenticated owner boot must notify the target and mutate central membership.'
}
if ($group -notmatch 'sessionId\s*==\s*0\s*\|\|\s*sessionId\s*==\s*request\.sessionId') {
    throw 'Boot routing must accept a valid request-scoped session on a multiplexed endpoint.'
}
$bootStart = $group.IndexOf('if (id == wire::kBootMachineId)')
$bootEnd = $group.IndexOf('if (id == wire::kPlayerAddId)', $bootStart)
if ($bootStart -lt 0 -or $bootEnd -le $bootStart) {
    throw 'Could not isolate boot-machine handling.'
}
$boot = $group.Substring($bootStart, $bootEnd - $bootStart)
if ($boot -notmatch 'member_address_for_recipient\(candidate,\s*\*source,\s*address\)') {
    throw 'Boot must resolve the target using the exact address key published to the owner.'
}
$centralMutation = $boot.IndexOf('remove_lobby_member(')
$bootNotification = $boot.IndexOf('wire::SessionMessageId::sessionBoot')
$gameplayRelease = $boot.IndexOf('release(targetEndpoint, request.sessionId)')
if ($centralMutation -lt 0 -or $bootNotification -lt 0 -or $gameplayRelease -lt 0 `
    -or $centralMutation -gt $bootNotification -or $bootNotification -gt $gameplayRelease) {
    throw 'Boot must commit central removal before ID18, then release gameplay membership.'
}
if ($service -notmatch 'reconcile_central_membership\(\)\s*;') {
    throw 'Gameplay service must reconcile normal central-lobby leave before delayed ID15.'
}

if ($realtime -notmatch 'constexpr\s+std::int32_t\s+kPeerEstablishedMode\s*=\s*4\s*;' `
    -or $realtime -notmatch 'exactNativeState[\s\S]*mode\s*==\s*kPeerEstablishedMode') {
    throw 'Joiner projection capture must use the observed peer-established mode 4 session.'
}
if ($bootstrap -match 'RouteSelectionIngest|enqueue_primary\(') {
    throw 'The retail manager pump may remain observed, but owner descriptor injection must stay disabled.'
}

$projectionServiceStart = $realtime.IndexOf(
    'void service_host_membership_projection(std::uint64_t nowMilliseconds) noexcept')
$projectionServiceEnd = $realtime.IndexOf("`nvoid queue_event(", $projectionServiceStart)
if ($projectionServiceStart -lt 0 -or $projectionServiceEnd -le $projectionServiceStart) {
    throw 'Could not isolate host membership projection service.'
}
$projectionService = $realtime.Substring(
    $projectionServiceStart,
    $projectionServiceEnd - $projectionServiceStart)
$applyProjection = $projectionService.IndexOf('apply_rebased_membership_projection(')
if ($applyProjection -lt 0 `
    -or $projectionService -notmatch 'event\.sourcePlatformId' `
    -or $projectionService -notmatch 'event\.lobbyId' `
    -or $projectionService -notmatch 'event\.sequence') {
    throw 'Host projection must bind the recipient transform to the owner, uploader, lobby, and event.'
}
$projectionApplyStart = $realtime.IndexOf(
    'NativeProjectionResult apply_rebased_membership_projection(')
$projectionApplyEnd = $realtime.IndexOf(
    '[[nodiscard]] bool arm_membership_projection_leave_suppression(',
    $projectionApplyStart)
if ($projectionApplyStart -lt 0 -or $projectionApplyEnd -le $projectionApplyStart) {
    throw 'Could not isolate the native owner projection apply.'
}
$projectionApply = $realtime.Substring(
    $projectionApplyStart,
    $projectionApplyEnd - $projectionApplyStart)
$rebaseProjection = $projectionApply.IndexOf('projection::rebase_solo_owner(')
$armProjectionLeave = $projectionApply.IndexOf(
    'arm_membership_projection_leave_suppression(lobbyId, eventSequence)')
$nativeProjectionApply = $projectionApply.IndexOf('apply_primary_membership(')
if ($rebaseProjection -lt 0 -or $armProjectionLeave -le $rebaseProjection `
    -or $nativeProjectionApply -le $armProjectionLeave) {
    throw 'Owner projection must validate/rebase, then arm the leave guard, then invoke native apply.'
}
if ($projectionApply -notmatch 'for\s*\(std::size_t\s+offset\s*=\s*0U;\s*offset\s*<\s*projection::kStateSize;' `
    -or $projectionApply -notmatch 'appliedState\[offset\]\s*!=\s*desiredState\[offset\]') {
    throw 'Owner projection must verify the complete applied canonical state, not only its hash.'
}
if ($projectionService -match 'retire_membership_projection_leave_suppression' `
    -or $projectionService -notmatch 'leaveSuppressionArmed\s*\?\s*"retained_async"') {
    throw 'Host projection must retain its one-shot leave guard for asynchronous native cleanup.'
}
if ($realtime -notmatch 'InternalOwnerTransition\s+g_membershipProjectionOwnerTransition\s*\{\s*\}\s*;' `
    -or $realtime -notmatch 'kMembershipProjectionLeaveSuppressionMilliseconds\s*=\s*5000\s*;' `
    -or $realtime -notmatch 'GetTickCount64\(\)\s*\+\s*kMembershipProjectionLeaveSuppressionMilliseconds' `
    -or $realtime -notmatch 'g_membershipProjectionOwnerTransition\.suppressLeavePending\s*=\s*false\s*;' `
    -or $realtime -notmatch 'g_membershipProjectionOwnerTransition\s*=\s*\{\s*\}\s*;') {
    throw 'Membership projection must use a distinct, bounded, one-call owner leave guard.'
}
if ($matchmaking -notmatch 'consume_internal_owner_leave_suppression\(lobby\)' `
    -or $matchmaking -notmatch 'leave_suppressed_internal_owner_transition') {
    throw 'Matchmaking leave must consume the internal native-session cleanup guard.'
}

if ($peer -notmatch 'constexpr bool kOpaquePlayerPropertiesRelayEnabled\s*=\s*false\s*;') {
    throw 'Opaque ID37 must not be rebound across dedicated-server channels.'
}

$relayStart = $peer.IndexOf('relay_player_properties(const state::gameplay::Endpoint& from,')
$relayEnd = $peer.IndexOf('find_session_locked(', $relayStart)
if ($relayStart -lt 0 -or $relayEnd -le $relayStart) {
    throw 'Could not isolate player-properties relay.'
}
$relay = $peer.Substring($relayStart, $relayEnd - $relayStart)
$cached = $relay.IndexOf('cache_player_properties_locked(')
if ($cached -lt 0 -or $relay -notmatch 'result\.suppressed\s*=\s*true') {
    throw 'ID37 must remain parseable but stop before the provenance-losing cache/fanout path.'
}
if ($relay -notmatch 'same_endpoint\(peer\.endpoint,\s*from\)') {
    throw 'Native ID37 eligible-peer accounting must exclude the publishing endpoint.'
}

$replayStart = $peer.IndexOf('replay_player_properties_locked(')
if ($replayStart -lt 0 -or $relayStart -le $replayStart) {
    throw 'Could not isolate retained player-properties replay.'
}
$replay = $peer.Substring($replayStart, $relayStart - $replayStart)
if ($replay -notmatch '!same_endpoint\(publication\.source,\s*peer\.endpoint\)') {
    throw 'Native ID37 replay must permanently exclude the publishing endpoint.'
}
if ($peer -notmatch 'discard_source_publications_locked\(endpoint,\s*sessionId\)' `
    -or $peer -notmatch 'discard_source_publications_locked\(endpoint\)') {
    throw 'Departed ID37 publishers must be purged so endpoint churn cannot replay a ghost profile.'
}

$parameterStart = $group.IndexOf('void apply_parameter_authority(')
$parameterEnd = $group.IndexOf('void answer_parameters(', $parameterStart)
if ($parameterStart -lt 0 -or $parameterEnd -le $parameterStart) {
    throw 'Could not isolate parameter-authority application.'
}
$parameter = $group.Substring($parameterStart, $parameterEnd - $parameterStart)
if ($parameter -match 'record->parameterAuthorityDirtyMask\s*\|=\s*suppliedMask') {
    throw 'Unchanged ID39 proposals must not re-arm an ID38 echo to the requester.'
}
if ($parameter -notmatch 'sourceIsLogicalOwner' `
    -or $parameter -notmatch 'acceptedSelectionSupplied\s*=\s*selectionSupplied\s*&&\s*sourceIsLogicalOwner') {
    throw 'Only the logical fireteam owner may author parameter 5; joiners may answer parameter 6.'
}

Write-Output 'PASS: shared fireteam rollback, private-parent PAH ownership, public-target own-session lifecycle, stable roster generation, central reconciliation, transactional boot, bounded authority, and ID37 quarantine are guarded'
