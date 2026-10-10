from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from package_reader import Packages, u32
from package_header_hashes import refresh_header_hashes


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def apply_edits(original, edits):
    result = bytearray(original)
    occupied = set()
    for edit in edits:
        offset = number(edit['offset'])
        before = bytes.fromhex(edit['before'])
        after = bytes.fromhex(edit['after'])
        if not before or len(before) != len(after):
            raise ValueError('Edits must be nonempty and preserve tag size')
        if offset < 0 or offset + len(before) > len(result):
            raise ValueError('Edit is outside the tag')
        indices = set(range(offset, offset + len(before)))
        if occupied & indices:
            raise ValueError('Overlapping edits')
        if original[offset:offset + len(before)] != before:
            raise ValueError(f'Preimage mismatch at tag offset {offset:#x}')
        result[offset:offset + len(after)] = after
        occupied.update(indices)
    return bytes(result)


def native_span_closure(entries, block_count, changed):
    """Keep every tag touching a relocated block in one native patch context. Native 32F1E0/330C10 picks the context from a tag's first block and 357040 keeps it for the 256 KiB continuation reads, so moving only a continuation block reads through the wrong patch file."""
    spans = []
    for at in range(0, len(entries), 16):
        placement = struct.unpack_from('<Q', entries, at + 8)[0]
        first = placement & 0x3fff
        offset = ((placement >> 14) & 0x3fff) * 16
        size = placement >> 28
        if not size:
            continue
        last = first + (offset + size - 1) // 0x40000
        if last >= block_count:
            raise ValueError('Tag span exceeds package block table')
        if last != first:
            spans.append((first, last))
    closure = set(changed)
    while True:
        previous = len(closure)
        for first, last in spans:
            if any(block in closure for block in range(first, last + 1)):
                closure.update(range(first, last + 1))
        if len(closure) == previous:
            return closure


def stage(packages, manifest, output):
    output = output.resolve()
    installed = (packages.root / 'packages').resolve()
    if output == installed or installed in output.parents:
        raise ValueError('Staging output must not be inside installed packages')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Staging output must be empty')
    changed = {}
    logical = {}
    for row in manifest['tags']:
        tag = number(row['tag'])
        if tag in logical:
            raise ValueError('Duplicate tag')
        cls, original = packages.read(tag)
        if cls != number(row['class']):
            raise ValueError('Tag class mismatch')
        if hashlib.sha256(original).hexdigest() != row['sha256'].lower():
            raise ValueError('Tag SHA256 mismatch')
        replacement = apply_edits(original, row['edits'])
        if replacement == original:
            raise ValueError('Tag has no effective edits')
        logical[tag] = replacement
        pid, index = (tag - 0x80800000) >> 13, tag & 0x1fff
        path, entries, _ = packages.table(pid)
        placement = struct.unpack_from('<Q', entries, index * 16 + 8)[0]
        block, offset = placement & 0x3fff, ((placement >> 14) & 0x3fff) * 16
        consumed = 0
        while consumed < len(original):
            source = packages.block(pid, block)
            count = min(len(source) - offset, len(original) - consumed)
            if count <= 0:
                raise ValueError('Invalid block span')
            segment = replacement[consumed:consumed + count]
            if segment != source[offset:offset + count]:
                target = changed.setdefault((pid, block), bytearray(source))
                if target[offset:offset + count] != source[offset:offset + count]:
                    raise ValueError('Overlapping tag placements')
                target[offset:offset + count] = segment
            consumed += count
            block += 1
            offset = 0
    edited_blocks = set(changed)
    for pid in sorted({key[0] for key in edited_blocks}):
        _, entries, block_table = packages.table(pid)
        closure = native_span_closure(entries, len(block_table) // 48,
                                      {block for item_pid, block in edited_blocks if item_pid == pid})
        for block in closure:
            if (pid, block) not in changed:
                changed[pid, block] = bytearray(packages.block(pid, block))
    output.mkdir(parents=True, exist_ok=True)
    receipts = []
    for pid in sorted({key[0] for key in changed}):
        path, entries, block_table = packages.table(pid)
        source = path.read_bytes()
        patch = packages.paths[pid][0]
        footer_at, file_size = u32(source, 0x160), u32(source, 0x164)
        if file_size != len(source) or footer_at + 0x800 != len(source):
            raise ValueError('Unsupported native package footer layout')
        if struct.unpack_from('<H', source, 0x20)[0] != patch:
            raise ValueError('Patch identity mismatch')
        table_at = u32(source, 0x110) + 96 + len(entries) + 32
        if table_at + len(block_table) > footer_at:
            raise ValueError('Block table overlaps footer')
        if table_at + len(block_table) > u32(source, 0x110) + u32(source, 0x114):
            raise ValueError('Block table exceeds native extended hash span')
        result = bytearray(source[:footer_at])
        blocks = []
        for (item_pid, index), payload in sorted(changed.items()):
            if item_pid != pid:
                continue
            result.extend(b'\0' * ((-len(result)) % 0x800))
            offset = len(result)
            result.extend(payload)
            descriptor = table_at + index * 48
            # plain blocks still carry their SHA1; the AES tag is unused with flags 0
            record = struct.pack('<IIHH', offset, len(payload), patch, 0)
            record += hashlib.sha1(payload).digest() + bytes(16)
            result[descriptor:descriptor + 48] = record
            blocks.append({'index': index, 'size': len(payload), 'offset': offset,
                           'logical_bytes_changed': (pid, index) in edited_blocks})
        result.extend(b'\0' * ((-len(result)) % 0x800))
        new_footer = len(result)
        result.extend(source[footer_at:])
        struct.pack_into('<II', result, 0x160, new_footer, len(result))
        header_hashes = refresh_header_hashes(result)
        destination = output / path.name
        destination.write_bytes(result)
        receipts.append({'package': path.name, 'source_sha256': hashlib.sha256(source).hexdigest(),
                         'staged_sha256': hashlib.sha256(result).hexdigest(),
                         'header_hashes': header_hashes, 'blocks': blocks})
    for (pid, block), payload in changed.items():
        staged = (output / packages.table(pid)[0].name).read_bytes()
        entries = packages.table(pid)[1]
        at = u32(staged, 0x110) + 96 + len(entries) + 32 + block * 48
        offset, size, patch, flags = struct.unpack_from('<IIHH', staged, at)
        assert flags == 0 and patch == packages.paths[pid][0]
        assert staged[offset:offset + size] == payload
        assert staged[at + 12:at + 32] == hashlib.sha1(payload).digest()
    report = {'status': 'staged_only', 'native_load_validated': False,
              'native_patch_span_closure': True,
              'packages': receipts, 'tags': list(manifest['tags'])}
    (output / 'staging-receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = stage(Packages(args.game_root, args.pid), json.loads(args.manifest.read_text()), args.output)
    print(json.dumps({'status': report['status'], 'packages': len(report['packages']),
                      'native_load_validated': False}))
