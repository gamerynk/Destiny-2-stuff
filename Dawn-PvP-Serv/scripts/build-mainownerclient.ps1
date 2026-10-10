param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $locator -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
$project = Join-Path $projectRoot "Dawn\Restoration.vcxproj"
$output = Join-Path $projectRoot "build\x64\ReleaseUniversal\"
$intermediate = Join-Path $projectRoot "build\obj\x64\ReleaseUniversal\"

if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found at $msbuild"
}

# Compatibility alias: PlayerOne uses the same universal binary as every other PC. Account
# identity comes from the preserved device key and backend link, never from a build-time owner ID.
& $msbuild $project /t:Rebuild /m:1 "/p:Configuration=$Configuration" /p:Platform=x64 `
    /p:MultiProcessorCompilation=false /p:UseMultiToolTask=false `
    /p:UniversalClient=true "/p:OutDir=$output" "/p:IntDir=$intermediate" /v:minimal /nologo
if ($LASTEXITCODE -ne 0) {
    throw "Universal client build failed with exit code $LASTEXITCODE"
}

$dll = Join-Path $output "steam_api64.dll"
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Universal client DLL was not produced."
}

$unicode = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($dll))
if (-not $unicode.Contains("device_identity.key")) {
    throw "Refusing artifact: DLL does not contain the universal device-identity path."
}

Get-FileHash -Algorithm SHA256 -LiteralPath $dll
