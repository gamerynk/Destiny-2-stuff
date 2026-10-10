from pathlib import Path

import argparse, gzip, hashlib, json, shutil, struct



CHUNK = 65536

GAME = '81964380664e7fcee3c620085a157fdeaf91fefacf7214907820f188bbeb4ced'

NETWORK = '39ec32dee02a0f3fac6a801490b0ff0868c976678626872242f083e34835aa51'



def sha(path):

    with Path(path).open('rb') as f:

        return hashlib.file_digest(f, 'sha256').hexdigest()



def native_hashes(path):

    with Path(path).open('rb') as f:

        h = f.read(0x170)

        if len(h) != 0x170 or struct.unpack_from('<H', h)[0] != 38:

            raise ValueError('Not a v38 package')

        footer, size = struct.unpack_from('<II', h, 0x160)

        if size != Path(path).stat().st_size or footer < 0x170 or footer + 0x800 != size:

            raise ValueError('Unsupported footer')

        rows = []

        for name, field in [('cached', 0xf0), ('extended', 0x110)]:

            offset, length = struct.unpack_from('<II', h, field)

            recorded = h[field+8:field+28].hex()

            if length:

                if offset < 0x12c or offset > footer or length > footer-offset:

                    raise ValueError('Invalid native hash span')

                f.seek(offset); dig = hashlib.sha1(); remaining = length

                while remaining:

                    data = f.read(min(CHUNK, remaining))

                    if not data: raise ValueError('Short native hash span')

                    dig.update(data); remaining -= len(data)

                if dig.hexdigest() != recorded: raise ValueError('Native metadata SHA1 mismatch')

            rows.append(dict(segment=name, offset=offset, bytes=length, sha1=recorded))

        return rows



def build_content(rows, output):

    output = Path(output); output.mkdir(parents=True, exist_ok=False)

    dedup = {}; packages = []

    with (output/'content.patch').open('wb') as blob:

        for row in rows:

            target = Path(row['target']); name = row['name']

            if '/' in name or '\\' in name or not name.startswith('w64_') or not name.endswith('.pkg'):

                raise ValueError('Invalid package name')

            final_hash = sha(target)

            if final_hash != row['sha256']: raise ValueError('Final package authority mismatch')

            variants = []

            for source_row in row['sources']:

                source = Path(source_row['path'])

                source_hash = sha(source)

                if source_hash != source_row['sha256']: raise ValueError('Qualified package preimage changed')

                if source_hash == final_hash or any(x['sha256'] == source_hash for x in variants): continue

                ops = []

                with source.open('rb') as old, target.open('rb') as new:

                    at = 0

                    while data := new.read(CHUNK):

                        before = old.read(len(data))

                        if before == data:

                            if ops and ops[-1]['kind']=='copy' and ops[-1]['offset']+ops[-1]['bytes']==at:

                                ops[-1]['bytes'] += len(data)

                            else: ops.append(dict(kind='copy', offset=at, bytes=len(data)))

                        else:

                            key = hashlib.sha256(data).hexdigest()

                            if key not in dedup:

                                encoded = gzip.compress(data, compresslevel=9, mtime=0)

                                dedup[key] = dict(blob_offset=blob.tell(), blob_bytes=len(encoded), bytes=len(data), sha256=key)

                                blob.write(encoded)

                            ops.append(dict(kind='data', **dedup[key]))

                        at += len(data)

                variants.append(dict(sha256=source_hash, bytes=source.stat().st_size, operations=ops))

            if not variants: raise ValueError('No qualified package preimage')

            packages.append(dict(name=name, sha256=final_hash, bytes=target.stat().st_size,

                                 native_hashes=native_hashes(target), sources=variants))

    return dict(format=1, package_count=len(packages), packages=packages, blob_sha256=sha(output/'content.patch'),

                blob_bytes=(output/'content.patch').stat().st_size)



def seal(output, client, template, game=GAME, network=NETWORK, endpoint=None, authority=None, server_endpoint=None):

    output = Path(output); client = Path(client)

    manifest = json.loads((output/'content-manifest.json').read_text())

    client_hash=sha(client)

    if authority:

        qualified=json.loads(Path(authority).read_text(encoding='utf-8-sig'))

        if qualified['artifacts']['steam_api64.dll'].lower()!=client_hash: raise ValueError('Build DLL authority mismatch')

        if any(qualified['pairedPackages'].get(p['name'],'').lower()!=p['sha256'] for p in manifest['packages']):

            raise ValueError('Build/package pairing mismatch')

        manifest['build_manifest_sha256']=sha(authority)

    shutil.copyfile(client, output/'steam_api64.dll')

    if sha(output/'steam_api64.dll')!=client_hash: raise ValueError('Copied DLL changed')

    manifest.update(game_sha256=game, network_sha256=network, client_sha256=client_hash,

                    client_bytes=client.stat().st_size, release_id='matched-'+sha(client)[:16], external_server=endpoint,

                    server_endpoint=server_endpoint)

    path = output/'release-manifest.json'

    path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')

    script = Path(template).read_text(encoding='utf-8').replace('{{MANIFEST_SHA256}}', sha(path))

    (output/'Install-Matched-Release.ps1').write_text(script, encoding='utf-8-sig')

    (output/'Install Update.cmd').write_text('@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Matched-Release.ps1" %*\r\npause\r\n')

    shutil.copyfile(Path(template).with_name('Launch-Matched-Dawn.ps1'),output/'Launch-Matched-Dawn.ps1')

    (output/'Launch Dawn.cmd').write_text('@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Matched-Dawn.ps1" %*\r\npause\r\n')

    (output/'README.txt').write_text('Dawn matched private server update\n\n1. Sign in to Steam. Close Destiny2.\n2. Extract the entire ZIP, then run Install Update.cmd. Enter your existing Dawn game folder when asked.\n3. Run Launch Dawn.cmd from this new package. It verifies the matched DLL/content before launching. Do not run an old installer afterward.\n\nInstall requires a complete matched2019 game and recognized native package preimages. It reconstructs the same40 corrected packages, checks both native metadata hashes, and updates the DLLs and verified private server endpoint together. Identity and unrelated settings remain intact. SteamVR is the launcher dependency; no VR headset is needed.\n\nBackups and the transaction receipt are kept in GAME\\Restoration\\matched-update-backups. To restore, close Destiny2 and run:\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File Install-Matched-Release.ps1 -Restore "FULL_PATH_TO_installation.json"\nRestore puts the prior DLLs, packages, settings and cache back together; it refuses unrelated later changes. -ContentOnly is available for local control installations that should retain their current endpoint.\n\nUse a matched client, server and content release. Offline packaging checks do not establish multiplayer compatibility.\n',encoding='utf-8')

    return manifest



def main():

    p=argparse.ArgumentParser(description=__doc__)

    p.add_argument('--plan', type=Path); p.add_argument('--output', required=True, type=Path)

    p.add_argument('--client', type=Path); p.add_argument('--seal-only', action='store_true')

    p.add_argument('--endpoint-settings', type=Path)

    p.add_argument('--build-manifest', type=Path)

    a=p.parse_args()

    if not a.seal_only:

        rows=json.loads(a.plan.read_text())['packages']

        if len(rows)!=40 or len({r['name'] for r in rows})!=40: raise ValueError('Expected exact40 distinct packages')

        manifest=build_content(rows,a.output)

        (a.output/'content-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

    endpoint=None; server_endpoint=None

    if a.endpoint_settings:

        settings=json.loads(a.endpoint_settings.read_text(encoding='utf-8-sig'))

        endpoint=settings['client']['external_server']; server=settings['server']; gameplay=server['gameplay']

        server_endpoint={key:server[key] for key in ('bap_advertised_address','bap_port','bap_advertised_port')}

        server_endpoint['gameplay']={key:gameplay[key] for key in ('topology','advertised_address','transport_address','port')}

        if not endpoint['enabled'] or gameplay['topology']=='disabled' or gameplay['advertised_address']!=gameplay['transport_address']:

            raise ValueError('Endpoint does not support native routes')

    if a.client:

        if not a.build_manifest: p.error('--client requires the reviewed --build-manifest')

        seal(a.output,a.client,Path(__file__).with_name('Install-Matched-Release.ps1'),endpoint=endpoint,authority=a.build_manifest,server_endpoint=server_endpoint)

    print(json.dumps(dict(output=str(a.output), sealed=bool(a.client), blob_bytes=(a.output/'content.patch').stat().st_size)))



if __name__=='__main__': main()

