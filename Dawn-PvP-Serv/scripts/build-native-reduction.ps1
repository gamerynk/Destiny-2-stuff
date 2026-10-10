param(
    [string]$Configuration = 'Release',
    [string]$LogName = 'native-reduction-build.log',
    [switch]$Trace,
    [switch]$AsyncOmission = $true,
    [switch]$SetCursorOmission = $true,
    [switch]$ClipOmission = $true,
    [switch]$KeyFocusOmission = $true,
    [switch]$WindowPresentation = $true,
    [switch]$PackageExtendedHash = $true,
    [switch]$PackageCachedHash = $true,
    [switch]$NativePackageTrust = $true,
    [switch]$NativeSteamTransport = $true,
    [switch]$NativeCombatPriority = $true,
    [switch]$NativeMatchPresentation = $true,
    [switch]$NativeWeaponPendingOmission = $true,
    [switch]$NativeWeaponApplyOmission = $true,
    [switch]$NativeAmmoObserverTrial = $true,
    [switch]$NativeAmmoHandoffTrial = $true
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $locator -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/amd64/MSBuild.exe' | Select-Object -First 1
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild)) {
    throw 'The x64 MSBuild toolchain was not found.'
}
if ([IO.Path]::GetFileName($LogName) -ne $LogName) {
    throw 'LogName must be a file name within the build directory.'
}
$buildRoot = Join-Path $projectRoot 'build'
[IO.Directory]::CreateDirectory($buildRoot) | Out-Null
$traceValue = if ($Trace) { 'true' } else { 'false' }
$asyncValue = if ($AsyncOmission) { 'true' } else { 'false' }
$clipValue = if ($ClipOmission) { 'true' } else { 'false' }
$keyFocusValue = if ($KeyFocusOmission) { 'true' } else { 'false' }
$windowPresentationValue = if ($WindowPresentation) { 'true' } else { 'false' }
$cursorValue = if ($SetCursorOmission) { 'true' } else { 'false' }
$extendedHashValue = if ($PackageExtendedHash -or $NativePackageTrust) { 'true' } else { 'false' }
$cachedHashValue = if ($PackageCachedHash -or $NativePackageTrust) { 'true' } else { 'false' }
$steamTransportValue = if ($NativeSteamTransport) { 'true' } else { 'false' }
$packageTrustValue = if ($NativePackageTrust) { 'true' } else { 'false' }
$combatPriorityValue = if ($NativeCombatPriority) { 'true' } else { 'false' }
$matchPresentationValue = if ($NativeMatchPresentation) { 'true' } else { 'false' }
$weaponPendingValue = if ($NativeWeaponPendingOmission) { 'true' } else { 'false' }
$weaponApplyValue = if ($NativeWeaponApplyOmission) { 'true' } else { 'false' }
$ammoObserverTrialValue = if ($NativeAmmoObserverTrial) { 'true' } else { 'false' }
$ammoHandoffTrialValue = if ($NativeAmmoHandoffTrial) { 'true' } else { 'false' }
# This profile requires the matching server and authored native-content package set.
# Keep both compiler and project fan-out at one on the shared game/build machine.
$buildArguments = @(
    (Join-Path $projectRoot 'restoration.sln'),
    "/p:Configuration=$Configuration", '/p:Platform=x64', '/p:UniversalClient=true',
    '/p:NativeNetworkRoutes=true', "/p:HookReductionTrace=$traceValue",
    '/p:NativeInputPause=true',
    "/p:NativeAsyncOmission=$asyncValue",
    "/p:NativeSetCursorOmission=$cursorValue",
    "/p:NativeClipOmission=$clipValue", "/p:NativeWindowPresentation=$windowPresentationValue",
    "/p:NativeKeyFocusOmission=$keyFocusValue",
    "/p:NativePackageExtendedHash=$extendedHashValue", "/p:NativePackageCachedHash=$cachedHashValue",
    "/p:NativePackageTrust=$packageTrustValue",
    "/p:NativeCombatPriority=$combatPriorityValue", "/p:NativeMatchPresentation=$matchPresentationValue",
    "/p:NativeWeaponPendingOmission=$weaponPendingValue", "/p:NativeWeaponApplyOmission=$weaponApplyValue",
    "/p:NativeAmmoObserverTrial=$ammoObserverTrialValue",
    "/p:NativeAmmoHandoffTrial=$ammoHandoffTrialValue",
    '/p:NativeCharacterSelection=true', '/p:NativeTowerCompletion=true',
    '/p:NativeRawMousePause=true', '/p:NativeInvestmentInvalidation=true',
    '/p:NativeBitmapReference=true', '/p:NativeProfileSetup=true',
    "/p:NativeSteamCertificate=$steamTransportValue", "/p:NativeSteamAuthentication=$steamTransportValue",
    '/p:NativePlayerBody=true', '/p:NativeOrbitHandoff=true',
    '/p:TransientRenderTarget=true',
    '/p:NativeResizeOmission=true', '/p:NativeCompositionPolicy=true',
    '/p:NativeFireteamCallback=true', '/p:NativeOrbitSliceSet=true',
    '/p:NativeCameraObservers=true',
    '/p:NativeSubscriptionResponses=false', '/p:PreferredToolArchitecture=x64',
    '/p:CL_MPCount=1', '/m:1', '/v:quiet', '/nologo', '/fl',
    "/flp:logfile=$buildRoot\$LogName"
)
Push-Location -LiteralPath $projectRoot
try {
    & $msbuild @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Native reduction build failed: $LASTEXITCODE" }
} finally { Pop-Location }
Get-FileHash -Algorithm SHA256 -LiteralPath @(
    (Join-Path $buildRoot "x64\$Configuration\steam_api64.dll"),
    (Join-Path $buildRoot "x64\$Configuration\restoration-server.exe")
)
