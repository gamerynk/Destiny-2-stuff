$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$lifecycle = Get-Content -LiteralPath "$root\Dawn\src\client\hooks\bootflow\bootflow_hook_lifecycle.cpp" -Raw
$internal = Get-Content -LiteralPath "$root\Dawn\src\client\hooks\bootflow\internal.h" -Raw
$policy = Get-Content -LiteralPath "$root\Dawn\src\core\runtime\runtime_policy.h" -Raw
$definition = Get-Content -LiteralPath "$root\Dawn\src\core\settings\client\definition.h" -Raw
$parser = Get-Content -LiteralPath "$root\Dawn\src\core\settings\client\client_settings_parser.cpp" -Raw
$defaults = Get-Content -LiteralPath "$root\Dawn\resources\default_settings.json" -Raw
$universal = Get-Content -LiteralPath "$root\Dawn\resources\universal_client_settings.json" -Raw
$project = Get-Content -LiteralPath "$root\Dawn\Restoration.vcxproj" -Raw
$joinPush = Get-Content -LiteralPath "$root\Dawn\src\server\bap\encrypted\push\activity\activity_message_push.cpp" -Raw

$forbidden = 'join_request_ready|forceJoinRequestReady|force_join_request_ready|kActivityJoinLaunchCompatibility'
if (Test-Path -LiteralPath "$root\Dawn\src\client\hooks\bootflow\join_request_ready.cpp") {
    throw 'The client join-readiness detour still exists.'
}
foreach ($source in @($lifecycle, $internal, $policy, $definition, $parser, $defaults, $universal, $project)) {
    if ($source -match $forbidden) {
        throw 'A client join-readiness force remains in the active source or settings.'
    }
}

$joinResult = $joinPush.IndexOf('service::join_result::encode_join_result(')
$entitySlots = $joinPush.IndexOf('append_entity_slot_notification(', $joinResult)
$globalState = $joinPush.IndexOf('append_global_state_notification(', $entitySlots)
if ($joinResult -lt 0 -or $entitySlots -le $joinResult -or $globalState -le $entitySlots) {
    throw 'The server join must publish result -> entity-slot latch -> global state in native order.'
}

Write-Output 'PASS: activity launch uses the native server join contract with no client readiness force'
