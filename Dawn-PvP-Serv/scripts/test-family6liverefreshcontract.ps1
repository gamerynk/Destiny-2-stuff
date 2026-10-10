$ErrorActionPreference = 'Stop'

$sessionPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\internal.h')
$publicationPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\queuez\definition.h')
$stagingPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\queuez\queuez_outcome_staging.cpp')
$deferredPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\queuez\queuez_deferred_push.cpp')
$requestPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\middleware\bap\family_unsubscription.h')
$familyStagingPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\bap\encrypted\queuez\staging\queuez_family_staging.cpp')

$session = Get-Content -LiteralPath $sessionPath -Raw
$publication = Get-Content -LiteralPath $publicationPath -Raw
$staging = Get-Content -LiteralPath $stagingPath -Raw
$deferred = Get-Content -LiteralPath $deferredPath -Raw
$request = Get-Content -LiteralPath $requestPath -Raw
$familyStaging = Get-Content -LiteralPath $familyStagingPath -Raw

foreach ($forbidden in @('fireteamRefreshDueTick', 'fireteamRefreshRoot',
        'fireteamRefreshFingerprint', 'fireteamRefreshObserved')) {
    if ($session -match [regex]::Escape($forbidden)) {
        throw "BAP Session still retains retired Family-6 replay state $forbidden."
    }
    if ($publication -match [regex]::Escape($forbidden)) {
        throw "QueueZ publication still arms retired Family-6 replay state $forbidden."
    }
}
foreach ($forbidden in @('consume_fireteam_refresh', 'fireteam_projection_fingerprint',
        'kFireteamRefreshPollMs', 'stage=fireteam_refresh')) {
    if ($deferred -match [regex]::Escape($forbidden)) {
        throw "Deferred output still publishes retired Family-6 FULL snapshots through $forbidden."
    }
}
if ($staging -match 'fireteamRefreshRoot' -or
    $staging -match 'familyType\s*==\s*middleware::datagen::kFireteamFamily') {
    throw 'Family-6 subscription staging still arms an unsolicited post-subscription replay.'
}

if ($request -notmatch 'std::uint8_t\s+familyType' -or $request -match 'std::uint8_t\s+flag') {
    throw 'Svc-14 byte zero must be modeled as the exact family selector.'
}
if ($staging -notmatch 'stage_unsubscription\(before,\s*outcome\.unsubscription\.familyType,\s*outcome\.unsubscription\.familyRootSoid') {
    throw 'Svc-14 family type and root must both reach QueueZ unsubscription staging.'
}
if ($familyStaging -notmatch 'familyType\s*==\s*kAccountFamilyType' -or
    $familyStaging -notmatch 'familyType\s*==\s*kRosterFamilyType') {
    throw 'QueueZ unsubscription must retire only the selected tracked family/root pair.'
}
if ($familyStaging -match '\(before\.family4Active\s*&&\s*familyRootSoid\s*==\s*before\.family4RootSoid\)\s*\|\|') {
    throw 'A same-root unsubscribe must not erase unrelated QueueZ families.'
}

Write-Output 'PASS: Family-6 is subscription-scoped, svc-14 is family-specific, and no retired FULL replay remains'
