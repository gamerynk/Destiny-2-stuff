from pathlib import Path
import hashlib,json,struct,subprocess,uuid
import build_matched_release as release

ROOT=Path(__file__).parent
W=ROOT.resolve().parents[1]/'build/tests/matched-release'/uuid.uuid4().hex; W.mkdir(parents=True)
checks=[]
def check(value,label):
    if not value: raise AssertionError(label)
    checks.append(label)
def package(path,change=False):
    body=bytearray((i*47+13)%251 for i in range(196608+(5000 if change else 0)))
    body[:0x170]=b'\0'*0x170; struct.pack_into('<H',body,0,38)
    struct.pack_into('<II',body,0x160,len(body)-0x800,len(body))
    for field,offset,count in [(0xf0,4096,117),(0x110,8192,777)]:
        struct.pack_into('<II',body,field,offset,count)
        if change: body[offset+3]^=0x33
        body[field+8:field+28]=hashlib.sha1(body[offset:offset+count]).digest()
    path.write_bytes(body)

source=W/'source.pkg'; target=W/'target.pkg'; package(source); package(target,True)
plan=[dict(name='w64_fixture_0001_0.pkg',target=str(target),sha256=release.sha(target),
           sources=[dict(path=str(source),sha256=release.sha(source))])]
out=W/'release'; manifest=release.build_content(plan,out)
(out/'content-manifest.json').write_text(json.dumps(manifest))
check(manifest['blob_bytes']<target.stat().st_size/3,'compact copy/data recipe')
client=W/'client.dll'; client.write_bytes(b'MATCHED_CLIENT')
game=W/'game'; (game/'bin/x64').mkdir(parents=True); (game/'packages').mkdir(); (game/'Restoration').mkdir()
(game/'destiny2.exe').write_bytes(b'FIXTURE_GAME'); (game/'bin/x64/steamnetworkingsockets.dll').write_bytes(b'FIXTURE_NETWORK')
endpoint=dict(enabled=True,host='verified.fixture',origin='http://verified.fixture:30977',http_port=30977,config_url='http://verified.fixture:30977/config/',port_routes=[[30974,30974]])
server_endpoint=dict(bap_advertised_address='192.0.2.10',bap_port=30974,bap_advertised_port=30974,
                     gameplay=dict(topology='embedded',advertised_address='192.0.2.10',transport_address='192.0.2.10',port=30976))
release.seal(out,client,ROOT/'Install-Matched-Release.ps1',release.sha(game/'destiny2.exe'),release.sha(game/'bin/x64/steamnetworkingsockets.dll'),endpoint,server_endpoint=server_endpoint)
shipped=json.loads((out/'release-manifest.json').read_text())
check(shipped['client_sha256']==release.sha(client),'DLL hash pinned')
for name in ['steam_api64.dll','bin/x64/steam_api64.dll']:(game/name).write_bytes(b'PREVIOUS_CLIENT')
(game/'packages/w64_fixture_0001_0.pkg').write_bytes(source.read_bytes())
settings={'steam':{'user':{'persona_name':'Identity \u2603'}},'client':{'external_server':{'host':'127.0.0.1'},'unrelated':123},'arbitrary':[1,{'keep':'unchanged'}],
          'server':{'bap_port':999,'unrelated_server':17,'entitlements':['KEEP'],'gameplay':{'bind_address':'127.0.0.1','server_reserve_count':256}}}
raw=json.dumps(settings,ensure_ascii=False).encode(); (game/'Restoration/settings.json').write_bytes(raw)
(game/'Restoration/player.json').write_bytes(b'EXACT_IDENTITY_BYTES'); (game/'cache_phr_fixture.dat').write_bytes(b'OLD_CACHE')
harness=W/'run.ps1'
harness.write_text(r'''
param([string]$Installer,[string]$Game,[string]$Payload)
$ErrorActionPreference='Stop'
. $Installer
function Assert-GameStopped {}
$checks=0
function Check($yes,$name){if(-not $yes){throw "FAIL $name"};$script:checks++;Write-Output "PASS $name"}
$originalSettings=[IO.File]::ReadAllBytes((Join-Path $Game 'Restoration/settings.json'))
$package=Join-Path $Game 'packages/w64_fixture_0001_0.pkg';$initial=Get-MatchedHash $package
$receipt=Invoke-MatchedInstall $Game $Payload
$r=Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json
Check ($r.status -eq 'installed') 'complete transaction'
Check ((Get-MatchedHash $package) -eq (Get-Content "$Payload/release-manifest.json" -Raw|ConvertFrom-Json).packages[0].sha256) 'exact final package'
$settings=Get-Content "$Game/Restoration/settings.json" -Raw -Encoding UTF8|ConvertFrom-Json
Check ($settings.client.external_server.host -eq 'verified.fixture' -and $settings.client.unrelated -eq 123 -and $settings.steam.user.persona_name -eq "Identity $([char]0x2603)" -and $settings.arbitrary[1].keep -eq 'unchanged') 'only connection settings changed'
Check ($settings.server.bap_advertised_address -eq '192.0.2.10' -and $settings.server.bap_port -eq 30974 -and $settings.server.bap_advertised_port -eq 30974 -and $settings.server.gameplay.topology -eq 'embedded' -and $settings.server.gameplay.advertised_address -eq '192.0.2.10' -and $settings.server.gameplay.transport_address -eq '192.0.2.10' -and $settings.server.gameplay.port -eq 30976) 'all native endpoint prerequisites published'
Check ($settings.server.unrelated_server -eq 17 -and $settings.server.entitlements[0] -eq 'KEEP' -and $settings.server.gameplay.bind_address -eq '127.0.0.1' -and $settings.server.gameplay.server_reserve_count -eq 256) 'other server settings preserved'
Check ([IO.File]::ReadAllText("$Game/Restoration/player.json") -eq 'EXACT_IDENTITY_BYTES') 'identity untouched'
Check (-not(Test-Path "$Game/cache_phr_fixture.dat")) 'changed package cache archived'
Check ((Invoke-MatchedInstall $Game $Payload) -eq 'Already installed; no files changed.') 'idempotent update'
$null=Restore-MatchedRelease $receipt
Check ((Get-MatchedHash $package) -eq $initial) 'exact package rollback'
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes("$Game/Restoration/settings.json")) -eq [Convert]::ToBase64String($originalSettings)) 'exact settings rollback'
Check ([IO.File]::ReadAllText("$Game/cache_phr_fixture.dat") -eq 'OLD_CACHE') 'cache restored'
Check ([IO.File]::ReadAllText("$Game/steam_api64.dll") -eq 'PREVIOUS_CLIENT' -and [IO.File]::ReadAllText("$Game/bin/x64/steam_api64.dll") -eq 'PREVIOUS_CLIENT') 'both prior DLLs restored'
# Fail after the first real target commit; actual catch/rollback must restore all.
$realPut=${function:Put-MatchedFile};$script:puts=0
function Put-MatchedFile([string]$Source,[string]$Target,[string]$Hash){$script:puts++;if($script:puts -eq 2){throw 'fixture failure after first commit'};& $realPut $Source $Target $Hash}
$failed=$false;try{$null=Invoke-MatchedInstall $Game $Payload}catch{$failed=$true}
Check ($failed -and (Get-MatchedHash $package) -eq $initial -and [IO.File]::ReadAllText("$Game/steam_api64.dll") -eq 'PREVIOUS_CLIENT') 'mid-commit automatic rollback'
${function:Put-MatchedFile}=$realPut
# Corrupt destination/source preimage: transaction must not touch DLL/settings.
[IO.File]::WriteAllText($package,'UNSUPPORTED')
$failed=$false;try{$null=Invoke-MatchedInstall $Game $Payload}catch{$failed=$true}
Check ($failed -and [IO.File]::ReadAllText("$Game/steam_api64.dll") -eq 'PREVIOUS_CLIENT') 'unknown preimage refuses before update'
[IO.File]::Copy((Join-Path (Split-Path -Parent $Payload) 'source.pkg'),$package,$true)
# Payload integrity and stopped-process checks.
$blob=Join-Path $Payload 'content.patch';$old=[IO.File]::ReadAllBytes($blob);[IO.File]::WriteAllText($blob,'CORRUPT')
$failed=$false;try{$null=Invoke-MatchedInstall $Game $Payload}catch{$failed=$true}
Check ($failed -and (Get-MatchedHash $package) -eq $initial) 'bad payload refuses'
[IO.File]::WriteAllBytes($blob,$old)
function Assert-GameStopped {throw 'fixture running game'}
$failed=$false;try{$null=Invoke-MatchedInstall $Game $Payload}catch{$failed=$true}
Check ($failed -and (Get-MatchedHash $package) -eq $initial) 'running game refuses'
function Assert-GameStopped {}
$receipt=Invoke-MatchedInstall $Game $Payload
[IO.File]::WriteAllText("$Game/steam_api64.dll",'LATER_UPDATE')
$failed=$false;try{$null=Restore-MatchedRelease $receipt}catch{$failed=$true}
Check ($failed -and [IO.File]::ReadAllText("$Game/steam_api64.dll") -eq 'LATER_UPDATE') 'rollback refuses later external changes'
$failed=$false;try{$null=Get-SafePath $Game '../escape'}catch{$failed=$true}
Check $failed 'path traversal refused'
Write-Output "TOTAL $checks actual PowerShell checks"
''',encoding='utf-8-sig')
result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(harness),'-Installer',str(out/'Install-Matched-Release.ps1'),'-Game',str(game),'-Payload',str(out)],capture_output=True,text=True)
print(result.stdout); print(result.stderr)
check(result.returncode==0,'actual PowerShell fixture succeeds')
bad=W/'bad.pkg'; bad.write_bytes(target.read_bytes()); data=bytearray(bad.read_bytes());data[8192]^=1;bad.write_bytes(data)
try: release.native_hashes(bad)
except ValueError:check(True,'native hash corruption refused')
else:check(False,'native hash corruption refused')
badPlan=[dict(plan[0],sources=[dict(path=str(source),sha256='0'*64)])]
try: release.build_content(badPlan,W/'bad-release')
except ValueError:check(True,'builder rejects changed qualified preimage')
else:check(False,'builder rejects changed qualified preimage')
report=dict(status='PASS',python_checks=checks,powershell_output=result.stdout,fixture_root=str(W),actual_source=True,live_install=False)
(W.parent/'fixture-test-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(status='PASS',python_checks=len(checks))))
