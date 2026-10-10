from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

GAME_SHA256 = '81964380664e7fcee3c620085a157fdeaf91fefacf7214907820f188bbeb4ced'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def contained(root, relative):
    relative = Path(relative)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Expected a relative path inside the installation')
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError('Path escapes the installation')
    return result


def ensure_stopped():
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq destiny2.exe', '/FO', 'CSV', '/NH'],
                            check=True, capture_output=True, text=True)
    if 'destiny2.exe' in result.stdout.lower():
        raise RuntimeError('Close Destiny 2 before installing or restoring the paired update')


def save(path, document):
    temporary = path.with_suffix('.json.pending')
    temporary.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def replace_verified(source, target, expected):
    temporary = target.with_name(target.name + '.native-' + uuid.uuid4().hex + '.pending')
    try:
        shutil.copyfile(source, temporary)
        if digest(temporary) != expected:
            raise ValueError('Copied file failed checksum verification')
        os.replace(temporary, target)
        if digest(target) != expected:
            raise ValueError('Installed file failed checksum verification')
    finally:
        if temporary.exists():
            temporary.unlink()


def restore(receipt_path, check_process=True):
    if check_process:
        ensure_stopped()
    receipt_path = receipt_path.resolve()
    record = json.loads(receipt_path.read_text(encoding='utf-8'))
    root = Path(record['game_root']).resolve()
    backup = receipt_path.parent
    # check all targets and backups first; an unrelated later update must not be overwritten by restoring this older one
    for row in record['files']:
        target = contained(root, row['relative'])
        source = contained(backup / 'files', row['relative'])
        if digest(source) != row['before']:
            raise ValueError('Backup checksum mismatch')
        if digest(target) not in (row['before'], row['after']):
            raise ValueError('A target changed after this installation; restore refused')
    record['status'] = 'restoring'
    save(receipt_path, record)
    for row in reversed(record['files']):
        replace_verified(contained(backup / 'files', row['relative']),
                         contained(root, row['relative']), row['before'])
    record['status'] = 'restored'
    save(receipt_path, record)
    return record


def install(root, client, packages, check_process=True, game_sha256=GAME_SHA256):
    if check_process:
        ensure_stopped()
    root, client, packages = root.resolve(), client.resolve(), packages.resolve()
    if digest(root / 'destiny2.exe') != game_sha256:
        raise ValueError('Game executable is not the supported build')
    receipt = json.loads((packages / 'staging-receipt.json').read_text(encoding='utf-8'))
    if receipt['status'] != 'staged_only' or not receipt['packages']:
        raise ValueError('Expected a nonempty native content staging receipt')
    client_hash = digest(client)
    files = []
    for relative in ('steam_api64.dll', 'bin/x64/steam_api64.dll'):
        target = contained(root, relative)
        files.append(dict(relative=relative, source=str(client), before=digest(target), after=client_hash))
    names = set()
    for row in receipt['packages']:
        name = row['package']
        if Path(name).name != name or not name.endswith('.pkg') or name in names:
            raise ValueError('Invalid or duplicate package filename')
        names.add(name)
        relative = 'packages/' + name
        target = contained(root, relative)
        source = contained(packages, name)
        if digest(target) != row['source_sha256'] or digest(source) != row['staged_sha256']:
            raise ValueError('Package source or candidate checksum mismatch')
        files.append(dict(relative=relative, source=str(source), before=row['source_sha256'],
                          after=row['staged_sha256']))
    backup = root / 'Restoration' / 'native-update-backups' / uuid.uuid4().hex
    backup.mkdir(parents=True)
    for row in files:
        target = contained(backup / 'files', row['relative'])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(contained(root, row['relative']), target)
        if digest(target) != row['before']:
            raise ValueError('Backup checksum mismatch; no files installed')
    record = dict(status='prepared', game_root=str(root), created_utc=datetime.now(timezone.utc).isoformat(),
                  files=files, native_body_loader_validated=False, server_changed=False)
    receipt_path = backup / 'installation.json'
    save(receipt_path, record)
    try:
        if check_process:
            ensure_stopped()
        if any(digest(contained(root, row['relative'])) != row['before'] for row in files):
            raise ValueError('Installation changed while preparing backups')
        record['status'] = 'installing'
        save(receipt_path, record)
        for row in files:
            replace_verified(Path(row['source']), contained(root, row['relative']), row['after'])
        record['status'] = 'installed_pending_live_validation'
        save(receipt_path, record)
    except Exception:
        restore(receipt_path, check_process=check_process)
        raise
    return receipt_path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--restore', type=Path)
    parser.add_argument('--game-root', type=Path)
    parser.add_argument('--client-dll', type=Path)
    parser.add_argument('--packages', type=Path)
    args = parser.parse_args()
    if args.restore:
        print(json.dumps({'status': restore(args.restore)['status']}))
    elif args.game_root and args.client_dll and args.packages:
        print(json.dumps({'receipt': str(install(args.game_root, args.client_dll, args.packages)),
                          'status': 'installed_pending_live_validation', 'server_changed': False}))
    else:
        parser.error('Supply --restore, or all of --game-root, --client-dll and --packages')
