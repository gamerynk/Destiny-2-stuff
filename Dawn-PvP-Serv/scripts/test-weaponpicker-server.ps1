[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ReferenceDirectory,
    [Parameter(Mandatory = $true)][string]$CompressionDll,
    [string]$ServerExe = (Join-Path $PSScriptRoot '..\build\x64\Release\restoration-server.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('..\build\tests\weapon-server-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$reference = (Resolve-Path -LiteralPath $ReferenceDirectory).Path
$server = (Resolve-Path -LiteralPath $ServerExe).Path
$compression = (Resolve-Path -LiteralPath $CompressionDll).Path
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'The test output directory must be new.' }
foreach ($relative in @('destiny2.exe', 'Restoration\settings.json', 'Restoration\cache\build_data.bin')) {
    if (!(Test-Path -LiteralPath (Join-Path $reference $relative) -PathType Leaf)) {
        throw "Missing matched test input: $relative"
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'Restoration\cache') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'bin\x64') | Out-Null
Copy-Item -LiteralPath $compression -Destination (Join-Path $destination 'bin\x64\oo2core_3_win64.dll')
Copy-Item -LiteralPath $server -Destination (Join-Path $destination 'restoration-server.exe')
foreach ($relative in @('destiny2.exe', 'Restoration\settings.json', 'Restoration\cache\build_data.bin')) {
    Copy-Item -LiteralPath (Join-Path $reference $relative) -Destination (Join-Path $destination $relative)
}
# The fixture creates its own database and identity key. No existing account storage is copied.
$testExe = Join-Path $destination 'restoration-server.exe'
& $testExe --build-data-self-test
if ($LASTEXITCODE -ne 0) { throw 'The matched build-data cache could not be loaded.' }
& $testExe --weapon-picker-self-test
if ($LASTEXITCODE -ne 0) { throw "Weapon picker test failed. Fixture retained at $destination" }
Write-Output "Weapon picker server checks passed. Fixture: $destination"
