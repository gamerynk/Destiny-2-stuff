$ErrorActionPreference = 'Stop'

$sourcePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Dawn\src\server\gameplay\group\group_host.cpp')
$source = Get-Content -LiteralPath $sourcePath -Raw

$joinStart = $source.IndexOf(
    'if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinComplete))')
$joinEnd = $source.IndexOf(
    'if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinAbort))',
    $joinStart + 1)
if ($joinStart -lt 0 -or $joinEnd -le $joinStart) {
    throw 'Could not isolate the joinComplete handler.'
}

$joinComplete = $source.Substring($joinStart, $joinEnd - $joinStart)
if ($joinComplete -match 'activity_host_session\s*\(' -or
    $joinComplete -match 'newest_joined_session\s*\(') {
    throw 'Plain-orbit joinComplete must not claim or inherit an activity-host session.'
}

$answerStart = $source.IndexOf('void answer_parameters(')
$answerEnd = $source.IndexOf('void answer_time(', $answerStart)
if ($answerStart -lt 0 -or $answerEnd -le $answerStart) {
    throw 'Could not isolate answer_parameters.'
}

$answerParameters = $source.Substring($answerStart, $answerEnd - $answerStart)
if ($answerParameters -notmatch '\(carried & activityHostBit\) != 0\s*&&\s*activity_host_session\s*\(') {
    throw 'The parameter-answer path may claim a host only for an explicit parameter-3 request.'
}

$publisherStart = $source.IndexOf('bool publish_activity_host_for_session(')
$publisherEnd = $source.IndexOf('bool publish_peer_reestablish_membership(', $publisherStart)
if ($publisherStart -lt 0 -or $publisherEnd -le $publisherStart) {
    throw 'Could not isolate publish_activity_host_for_session.'
}

$publisher = $source.Substring($publisherStart, $publisherEnd - $publisherStart)
if ($publisher -notmatch 'held_host_session\(parameterSessionId\)\s*==\s*state::activity::kAbsentSessionId') {
    throw 'Activity-host publication must fail closed until a real host session exists.'
}

Write-Output 'PASS: plain-orbit Join cannot claim or publish an activity-host session'
