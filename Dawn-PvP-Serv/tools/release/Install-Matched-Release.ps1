param([string]$GameFolder,[string]$Restore,[switch]$ContentOnly)
$ErrorActionPreference='Stop'
$MatchedPackageRoot=$PSScriptRoot
$MatchedManifestPin='{{MANIFEST_SHA256}}'
function Assert-GameStopped {
    if (Get-Process -Name destiny2 -ErrorAction SilentlyContinue) { throw 'Close Destiny 2 before updating or restoring.' }
}
function Get-MatchedHash([string]$Path) {
    $stream=[IO.File]::OpenRead($Path); $hash=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
function Get-SafePath([string]$Root,[string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative -match ':') { throw 'Unsafe release path.' }
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $path=[IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $path.StartsWith($rootPath+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped installation.' }
    $check=$path
    while ($check -and $check.StartsWith($rootPath,[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $check) {
            if ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation paths are unsupported.' }
        }
        if ($check -eq $rootPath) { break }; $check=Split-Path -Parent $check
    }
    return $path
}
function Read-Exact($Stream,[byte[]]$Buffer,[int]$Count) {
    $done=0
    while ($done -lt $Count) { $n=$Stream.Read($Buffer,$done,$Count-$done); if ($n -eq 0) { throw 'Unexpected end of release data.' }; $done+=$n }
}
function Get-SpanSHA1($Stream,[long]$Offset,[long]$Count) {
    $Stream.Position=$Offset; $hash=[Security.Cryptography.SHA1]::Create(); $buffer=New-Object byte[] 65536
    try {
        while ($Count -gt 0) { $n=[int][Math]::Min(65536,$Count); Read-Exact $Stream $buffer $n; $null=$hash.TransformBlock($buffer,0,$n,$buffer,0); $Count-=$n }
        $null=$hash.TransformFinalBlock([byte[]]@(),0,0); return ([BitConverter]::ToString($hash.Hash)).Replace('-','').ToLowerInvariant()
    } finally { $hash.Dispose() }
}
function Assert-NativeHashes([string]$Path,$Rows) {
    $f=[IO.File]::OpenRead($Path)
    try {
        $h=New-Object byte[] 368; Read-Exact $f $h 368
        $footer=[BitConverter]::ToUInt32($h,352)
        if ([BitConverter]::ToUInt16($h,0) -ne 38 -or [BitConverter]::ToUInt32($h,356) -ne $f.Length -or $footer -lt 368 -or $footer+2048 -ne $f.Length) { throw 'Invalid native package layout.' }
        if (@($Rows).Count -ne 2) { throw 'Missing native hash records.' }
        foreach($row in $Rows) {
            if ($row.segment -eq 'cached') { $field=240 } elseif ($row.segment -eq 'extended') { $field=272 } else { throw 'Invalid hash record.' }
            $start=[BitConverter]::ToUInt32($h,$field); $count=[BitConverter]::ToUInt32($h,$field+4)
            $recorded=([BitConverter]::ToString($h,$field+8,20)).Replace('-','').ToLowerInvariant()
            if ($start -ne $row.offset -or $count -ne $row.bytes -or $recorded -ne $row.sha1) { throw 'Native hash authority mismatch.' }
            if ($count -gt 0) {
                if ($start -lt 300 -or $start -gt $footer -or $count -gt $footer-$start -or (Get-SpanSHA1 $f $start $count) -ne $recorded) { throw 'Native metadata checksum mismatch.' }
            }
        }
    } finally { $f.Dispose() }
}
function Save-MatchedReceipt([string]$Path,$Record) {
    $pending=$Path+'.pending'; [IO.File]::WriteAllText($pending,($Record|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($pending,$Path,[NullString]::Value) } else { [IO.File]::Move($pending,$Path) }
}
function Put-MatchedFile([string]$Source,[string]$Target,[string]$Hash) {
    $pending=$Target+'.matched-'+[guid]::NewGuid().ToString('N')+'.pending'
    try {
        [IO.File]::Copy($Source,$pending,$false)
        if ((Get-MatchedHash $pending) -ne $Hash) { throw 'Copied release file checksum mismatch.' }
        if (Test-Path -LiteralPath $Target) { [IO.File]::Replace($pending,$Target,[NullString]::Value) } else { [IO.File]::Move($pending,$Target) }
        if ((Get-MatchedHash $Target) -ne $Hash) { throw 'Installed release file checksum mismatch.' }
    } finally { if (Test-Path -LiteralPath $pending) { [IO.File]::Delete($pending) } }
}
function Expand-MatchedPackage([string]$Source,[string]$Blob,[string]$Destination,$Variant,$Package) {
    $old=[IO.File]::OpenRead($Source); $blobFile=[IO.File]::OpenRead($Blob); $new=[IO.File]::Open($Destination,[IO.FileMode]::CreateNew)
    try {
        $buffer=New-Object byte[] 65536; [long]$total=0
        foreach ($op in $Variant.operations) {
            [long]$length=$op.bytes
            if ($length -le 0 -or $total -gt $Package.bytes-$length) { throw 'Invalid recipe output range.' }
            if ($op.kind -eq 'copy') {
                if ($op.offset -lt 0 -or $op.offset -gt $old.Length -or $length -gt $old.Length-$op.offset) { throw 'Invalid recipe source range.' }
                $old.Position=$op.offset; $left=$length
                while ($left -gt 0) { $n=[int][Math]::Min(65536,$left); Read-Exact $old $buffer $n; $new.Write($buffer,0,$n); $left-=$n }
            } elseif ($op.kind -eq 'data') {
                if ($length -gt 65536 -or $op.blob_bytes -le 0 -or $op.blob_bytes -gt 131072 -or $op.blob_offset -lt 0 -or $op.blob_offset -gt $blobFile.Length-$op.blob_bytes) { throw 'Invalid recipe data range.' }
                $blobFile.Position=$op.blob_offset; $packed=New-Object byte[] ([int]$op.blob_bytes); Read-Exact $blobFile $packed $packed.Length
                $memory=[IO.MemoryStream]::new($packed,$false); $gzip=[IO.Compression.GZipStream]::new($memory,[IO.Compression.CompressionMode]::Decompress)
                try {
                    Read-Exact $gzip $buffer ([int]$length)
                    if ($gzip.ReadByte() -ne -1) { throw 'Oversized recipe data.' }
                    $hash=[Security.Cryptography.SHA256]::Create()
                    try { $actual=([BitConverter]::ToString($hash.ComputeHash($buffer,0,[int]$length))).Replace('-','').ToLowerInvariant() } finally { $hash.Dispose() }
                    if ($actual -ne $op.sha256) { throw 'Recipe chunk checksum mismatch.' }
                    $new.Write($buffer,0,[int]$length)
                } finally { $gzip.Dispose(); $memory.Dispose() }
            } else { throw 'Unknown package operation.' }
            $total+=$length
        }
        if ($total -ne $Package.bytes) { throw 'Incomplete package recipe.' }
    } finally { $old.Dispose(); $blobFile.Dispose(); $new.Dispose() }
    if ((Get-MatchedHash $Destination) -ne $Package.sha256) { throw 'Reconstructed package checksum mismatch.' }
    Assert-NativeHashes $Destination $Package.native_hashes
}
function Restore-MatchedRelease([string]$Receipt) {
    Assert-GameStopped
    $Receipt=(Resolve-Path -LiteralPath $Receipt).Path; $record=Get-Content -LiteralPath $Receipt -Raw -Encoding UTF8|ConvertFrom-Json
    $root=$record.game_root; $backup=Split-Path -Parent $Receipt
    foreach($row in $record.files) {
        $target=Get-SafePath $root $row.relative
        $current=if(Test-Path -LiteralPath $target){Get-MatchedHash $target}else{$null}
        if ($current -ne $row.before -and $current -ne $row.after) { throw 'A file changed after installation; rollback refused.' }
        if ($row.before -and (Get-MatchedHash (Get-SafePath (Join-Path $backup 'files') $row.relative)) -ne $row.before) { throw 'Backup checksum mismatch.' }
    }
    $record.status='restoring'; Save-MatchedReceipt $Receipt $record
    foreach($row in @($record.files)[($record.files.Count-1)..0]) {
        Assert-GameStopped
        $target=Get-SafePath $root $row.relative
        $current=if(Test-Path -LiteralPath $target){Get-MatchedHash $target}else{$null}
        if($current -ne $row.before -and $current -ne $row.after){throw 'A file changed during rollback; restoration stopped.'}
        if ($row.before) { Put-MatchedFile (Get-SafePath (Join-Path $backup 'files') $row.relative) $target $row.before }
        elseif(Test-Path -LiteralPath $target) { [IO.File]::Delete($target) }
    }
    $record.status='restored'; Save-MatchedReceipt $Receipt $record; return $Receipt
}
function Invoke-MatchedInstall([string]$Root,[string]$PackageRoot=$MatchedPackageRoot,[switch]$KeepConnection) {
    Assert-GameStopped; $Root=(Resolve-Path -LiteralPath $Root).Path
    $manifestPath=Get-SafePath $PackageRoot 'release-manifest.json'
    if ((Get-MatchedHash $manifestPath) -ne $MatchedManifestPin) { throw 'Release manifest checksum mismatch.' }
    $m=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
    if ($m.format -ne 1 -or @($m.packages).Count -ne $m.package_count -or @($m.packages.name|Sort-Object -Unique).Count -ne $m.package_count) { throw 'Invalid release package set.' }
    if ((Get-MatchedHash (Get-SafePath $Root 'destiny2.exe')) -ne $m.game_sha256 -or (Get-MatchedHash (Get-SafePath $Root 'bin/x64/steamnetworkingsockets.dll')) -ne $m.network_sha256) { throw 'A matched 2019 game installation is required.' }
    $client=Get-SafePath $PackageRoot 'steam_api64.dll'; $blob=Get-SafePath $PackageRoot 'content.patch'
    if ((Get-MatchedHash $client) -ne $m.client_sha256 -or (Get-MatchedHash $blob) -ne $m.blob_sha256 -or (Get-Item -LiteralPath $blob).Length -ne $m.blob_bytes) { throw 'Release payload checksum mismatch.' }
    $files=[Collections.Generic.List[object]]::new(); $changedPackages=$false
    foreach($p in $m.packages) {
        if ($p.name -notmatch '^w64_[a-zA-Z0-9_]+\.pkg$') { throw 'Invalid package name.' }
        $relative='packages/'+$p.name; $target=Get-SafePath $Root $relative; $before=Get-MatchedHash $target
        if ($before -eq $p.sha256) { Assert-NativeHashes $target $p.native_hashes; continue }
        $v=@($p.sources|Where-Object sha256 -eq $before)
        if ($v.Count -ne 1 -or (Get-Item -LiteralPath $target).Length -ne $v[0].bytes) { throw ('Unsupported package preimage: '+$p.name+'. No files changed.') }
        $files.Add([pscustomobject]@{relative=$relative;before=$before;after=$p.sha256;package=$p;variant=$v[0];source=$target;kind='package'}); $changedPackages=$true
    }
    foreach($relative in @('steam_api64.dll','bin/x64/steam_api64.dll')) {
        $target=Get-SafePath $Root $relative; $before=if(Test-Path -LiteralPath $target){Get-MatchedHash $target}else{$null}
        if ($before -ne $m.client_sha256) { $files.Add([pscustomobject]@{relative=$relative;before=$before;after=$m.client_sha256;source=$client;kind='file'}) }
    }
    if (-not $KeepConnection) {
        if (-not $m.external_server -or -not $m.external_server.host -or -not $m.external_server.origin -or -not $m.server_endpoint -or -not $m.server_endpoint.bap_advertised_address -or -not $m.server_endpoint.gameplay.topology) { throw 'Release has no verified private server native-route endpoint; use ContentOnly only for a local control installation.' }
        foreach($relative in @('Restoration/settings.json','bin/x64/Restoration/settings.json')) {
            $target=Get-SafePath $Root $relative
            if (-not(Test-Path -LiteralPath $target)) { if($relative -eq 'Restoration/settings.json'){throw 'Existing Dawn settings required.'}; continue }
            $settings=Get-Content -LiteralPath $target -Raw -Encoding UTF8|ConvertFrom-Json
            $beforeSettings=$settings|ConvertTo-Json -Depth 100 -Compress
            if (-not $settings.client) { $settings|Add-Member -NotePropertyName client -NotePropertyValue ([pscustomobject]@{}) }
            $settings.client|Add-Member -NotePropertyName external_server -NotePropertyValue $m.external_server -Force
            if (-not $settings.server) { $settings|Add-Member -NotePropertyName server -NotePropertyValue ([pscustomobject]@{}) }
            foreach($field in @('bap_advertised_address','bap_port','bap_advertised_port')) { $settings.server|Add-Member -NotePropertyName $field -NotePropertyValue $m.server_endpoint.$field -Force }
            if (-not $settings.server.gameplay) { $settings.server|Add-Member -NotePropertyName gameplay -NotePropertyValue ([pscustomobject]@{}) }
            foreach($field in @('topology','advertised_address','transport_address','port')) { $settings.server.gameplay|Add-Member -NotePropertyName $field -NotePropertyValue $m.server_endpoint.gameplay.$field -Force }
            if (($settings|ConvertTo-Json -Depth 100 -Compress) -eq $beforeSettings) { continue }
            $files.Add([pscustomobject]@{relative=$relative;before=(Get-MatchedHash $target);after=$null;text=($settings|ConvertTo-Json -Depth 100);kind='settings'})
        }
    }
    if ($changedPackages) {
        foreach($cache in Get-ChildItem -LiteralPath $Root -File -Filter 'cache_phr_*.dat') {
            $target=Get-SafePath $Root $cache.Name
            $files.Add([pscustomobject]@{relative=$cache.Name;before=(Get-MatchedHash $target);after=$null;kind='cache'})
        }
    }
    if ($files.Count -eq 0) { return 'Already installed; no files changed.' }
    [long]$required=67108864; [long]$largest=0
    foreach($row in $files) {
        if($row.before){$required+=(Get-Item -LiteralPath (Get-SafePath $Root $row.relative)).Length}
        if($row.kind -eq 'package'){$size=[long]$row.package.bytes}
        elseif($row.kind -eq 'file'){$size=[long]$m.client_bytes}
        elseif($row.kind -eq 'settings'){$size=[long][Text.Encoding]::UTF8.GetByteCount($row.text)}
        else{$size=0}
        $required+=$size; $largest=[Math]::Max($largest,$size)
    }
    $required+=$largest # One same-volume atomic replacement copy while staged files remain.
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($Root))
    if(-not $drive.IsReady -or $drive.AvailableFreeSpace -lt $required){throw 'Insufficient space for complete staged output and verified backups; no files changed.'}
    $backup=Get-SafePath $Root ('Restoration/matched-update-backups/'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($backup)|Out-Null
    $receipt=Join-Path $backup 'installation.json'; $record=[ordered]@{format=1;status='preparing';release_id=$m.release_id;game_root=$Root;created_utc=[DateTime]::UtcNow.ToString('o');files=@()}
    # Prepare every output and every verified backup before replacing a target.
    foreach($row in $files) {
        $stage=Get-SafePath (Join-Path $backup 'staged') $row.relative; [IO.Directory]::CreateDirectory((Split-Path -Parent $stage))|Out-Null
        if($row.kind -eq 'package'){ Expand-MatchedPackage $row.source $blob $stage $row.variant $row.package }
        elseif($row.kind -eq 'settings'){ [IO.File]::WriteAllText($stage,$row.text,[Text.UTF8Encoding]::new($false)); $row.after=Get-MatchedHash $stage }
        elseif($row.kind -eq 'file'){ [IO.File]::Copy($row.source,$stage); if((Get-MatchedHash $stage) -ne $row.after){throw 'Staged DLL mismatch.'} }
        if($row.before) {
            $saved=Get-SafePath (Join-Path $backup 'files') $row.relative; [IO.Directory]::CreateDirectory((Split-Path -Parent $saved))|Out-Null
            [IO.File]::Copy((Get-SafePath $Root $row.relative),$saved)
            if((Get-MatchedHash $saved) -ne $row.before){throw 'Backup mismatch; no targets changed.'}
        }
        $record.files+=@([ordered]@{relative=$row.relative;before=$row.before;after=$row.after})
    }
    $record.status='prepared'; Save-MatchedReceipt $receipt $record
    try {
        Assert-GameStopped
        foreach($row in $files) {
            $target=Get-SafePath $Root $row.relative; $current=if(Test-Path -LiteralPath $target){Get-MatchedHash $target}else{$null}
            if($current -ne $row.before){throw 'Installation changed during preparation.'}
        }
        $record.status='installing'; Save-MatchedReceipt $receipt $record
        foreach($row in $files) {
            Assert-GameStopped
            $target=Get-SafePath $Root $row.relative
            $current=if(Test-Path -LiteralPath $target){Get-MatchedHash $target}else{$null}
            if($current -ne $row.before){throw 'A target changed during commit; replacement refused.'}
            if($row.kind -eq 'cache'){[IO.File]::Delete($target)}
            else{Put-MatchedFile (Get-SafePath (Join-Path $backup 'staged') $row.relative) $target $row.after}
        }
        $record.status='installed'; Save-MatchedReceipt $receipt $record
    } catch {
        $failure=$_
        try { $null=Restore-MatchedRelease $receipt } catch { throw ('Update failed; rollback also refused: '+$_.Exception.Message+'. Receipt: '+$receipt) }
        throw $failure
    }
    return $receipt
}
if ($MyInvocation.InvocationName -ne '.') {
    if ($Restore) { Restore-MatchedRelease $Restore }
    else { if(-not $GameFolder){$GameFolder=Read-Host 'Existing Dawn game folder containing destiny2.exe'}; $GameFolder=$GameFolder.Trim().Trim('"'); Invoke-MatchedInstall $GameFolder $MatchedPackageRoot -KeepConnection:$ContentOnly }
    Write-Host 'The game was not launched. Identity files and unrelated settings were preserved.'
}
