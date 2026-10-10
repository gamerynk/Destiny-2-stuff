param([string]$GameFolder,[string]$SteamApi)
$ErrorActionPreference='Stop'
$launchFolder=$GameFolder
. (Join-Path $PSScriptRoot 'Install-Matched-Release.ps1')
$GameFolder=$launchFolder
if(-not $GameFolder){$GameFolder=Read-Host 'Dawn game folder containing destiny2.exe'}
$GameFolder=(Resolve-Path -LiteralPath $GameFolder.Trim().Trim('"')).Path
Assert-GameStopped
$manifestPath=Get-SafePath $MatchedPackageRoot 'release-manifest.json'
if((Get-MatchedHash $manifestPath) -ne $MatchedManifestPin){throw 'Release manifest checksum mismatch.'}
$m=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$game=Get-SafePath $GameFolder 'destiny2.exe'
if((Get-MatchedHash $game) -ne $m.game_sha256 -or (Get-MatchedHash (Get-SafePath $GameFolder 'bin/x64/steamnetworkingsockets.dll')) -ne $m.network_sha256){throw 'The game is not the matched 2019 installation.'}
foreach($relative in @('steam_api64.dll','bin/x64/steam_api64.dll')){
    if((Get-MatchedHash (Get-SafePath $GameFolder $relative)) -ne $m.client_sha256){throw 'Run Install Update.cmd first; the installed client does not match this release.'}
}
foreach($p in $m.packages){
    if((Get-MatchedHash (Get-SafePath $GameFolder ('packages/'+$p.name))) -ne $p.sha256){throw 'Run Install Update.cmd first; native content does not match this release.'}
}
$settings=Get-Content -LiteralPath (Get-SafePath $GameFolder 'Restoration/settings.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($m.external_server){
    if(($settings.client.external_server|ConvertTo-Json -Depth 100 -Compress) -ne ($m.external_server|ConvertTo-Json -Depth 100 -Compress)){throw 'private server endpoint settings differ; run Install Update.cmd first.'}
    foreach($field in @('bap_advertised_address','bap_port','bap_advertised_port')){if($settings.server.$field -ne $m.server_endpoint.$field){throw 'Native relay settings differ; run Install Update.cmd first.'}}
    foreach($field in @('topology','advertised_address','transport_address','port')){if($settings.server.gameplay.$field -ne $m.server_endpoint.gameplay.$field){throw 'Native gameplay settings differ; run Install Update.cmd first.'}}
}
$activeSteam=Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam\ActiveProcess' -Name ActiveUser -ErrorAction SilentlyContinue
if(-not(Get-Process -Name steam -ErrorAction SilentlyContinue) -or -not $activeSteam -or $activeSteam.ActiveUser -eq 0){throw 'Open Steam, sign in and wait for the Library before launching.'}
if(-not $SteamApi){$SteamApi=$env:RESTORATION_STEAM_API_PATH}
if(-not $SteamApi){
    $steamRoots=@((Join-Path ${env:ProgramFiles(x86)} 'Steam'))
    $registered=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if($registered){$steamRoots+=$registered}
    foreach($steamRoot in @($steamRoots)){
        $libraries=Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
        if(Test-Path -LiteralPath $libraries){foreach($match in [regex]::Matches([IO.File]::ReadAllText($libraries),'"path"\s+"([^"]+)"')){$steamRoots+=$match.Groups[1].Value.Replace('\\','\')}}
    }
    foreach($steamRoot in ($steamRoots|Select-Object -Unique)){
        $candidate=Join-Path $steamRoot 'steamapps/common/SteamVR/bin/win64/steam_api64.dll'
        if(Test-Path -LiteralPath $candidate){$SteamApi=$candidate;break}
    }
}
if(-not $SteamApi -or -not(Test-Path -LiteralPath $SteamApi)){throw 'Install SteamVR through Steam (no headset needed), or provide -SteamApi pointing to its bin/win64/steam_api64.dll.'}
$env:RESTORATION_STEAM_API_PATH=(Resolve-Path -LiteralPath $SteamApi).Path
# No installer or copying occurs here. The user runs this launcher explicitly.
Start-Process -FilePath $game -WorkingDirectory $GameFolder
