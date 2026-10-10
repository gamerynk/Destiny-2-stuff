import argparse
import hashlib
import json
from pathlib import Path
import struct

from package_reader import Packages, u32


def verify(packages, staged):
    receipt = json.loads((staged / 'staging-receipt.json').read_text())
    expected = {}
    for tag in receipt['tags']:
        datum = int(tag['tag'], 0)
        pid, index = (datum - 0x80800000) >> 13, datum & 8191
        _, entries, _ = packages.table(pid)
        placement = struct.unpack_from('<Q', entries, index * 16 + 8)[0]
        start_block = placement & 0x3fff
        start_offset = ((placement >> 14) & 0x3fff) * 16
        cls, body = packages.read(datum)
        assert cls == int(tag['class'], 0)
        assert hashlib.sha256(body).hexdigest() == tag['sha256'].lower()
        for edit in tag['edits']:
            at = int(edit['offset'], 0) if isinstance(edit['offset'], str) else edit['offset']
            before, after = bytes.fromhex(edit['before']), bytes.fromhex(edit['after'])
            assert len(before) == len(after)
            assert body[at:at + len(before)] == before
            for i, (old, new) in enumerate(zip(before, after)):
                if old == new:
                    continue
                absolute = start_offset + at + i
                key = (pid, start_block + absolute // 0x40000)
                byte_at = absolute % 0x40000
                assert byte_at not in expected.setdefault(key, {})
                expected[key][byte_at] = (old, new)
    required = {}
    for pid, _ in expected:
        if pid in required:
            continue
        _, entries, table = packages.table(pid)
        selected = {block for item_pid, block in expected if item_pid == pid}
        spans = []
        for at in range(0, len(entries), 16):
            value = struct.unpack_from('<Q', entries, at + 8)[0]
            first, skip, size = value & 0x3fff, ((value >> 14) & 0x3fff) * 16, value >> 28
            if not size:
                continue
            last = first + (skip + size - 1) // 0x40000
            assert last < len(table) // 48
            spans.append(set(range(first, last + 1)))
        while True:
            previous = selected.copy()
            for span in spans:
                if span & selected:
                    selected |= span
            if selected == previous:
                break
        required[pid] = selected
    checked = set()
    total_entries = total_changed = 0
    for row in receipt['packages']:
        candidate = (staged / row['package']).read_bytes()
        pid = struct.unpack_from('<H', candidate, 4)[0]
        original_path, entries, blocks = packages.table(pid)
        original = original_path.read_bytes()
        assert hashlib.sha256(original).hexdigest() == row['source_sha256']
        assert hashlib.sha256(candidate).hexdigest() == row['staged_sha256']
        old_footer, old_size = struct.unpack_from('<II', original, 0x160)
        new_footer, new_size = struct.unpack_from('<II', candidate, 0x160)
        assert old_size == len(original) and new_size == len(candidate)
        assert candidate[new_footer:] == original[old_footer:]
        assert len(candidate[new_footer:]) == 0x800
        table_at = u32(original, 0x110) + 96 + len(entries) + 32
        restored_prefix = bytearray(candidate[:old_footer])
        restored_prefix[0x160:0x168] = original[0x160:0x168]
        assert struct.unpack_from('<H', candidate)[0] == 38
        for field in (0xf0, 0x110):
            start, count = struct.unpack_from('<II', candidate, field)
            assert candidate[field:field + 8] == original[field:field + 8]
            if count:
                assert 0x12c <= start <= new_footer and count <= new_footer - start
                assert candidate[field + 8:field + 28] == hashlib.sha1(candidate[start:start + count]).digest()
            else:
                assert candidate[field + 8:field + 28] == original[field + 8:field + 28]
            restored_prefix[field + 8:field + 28] = original[field + 8:field + 28]
        for block in row['blocks']:
            index = block['index']
            key = (pid, index)
            assert index in required[pid] and key not in checked
            checked.add(key)
            at = table_at + index * 48
            offset, size, patch, flags = struct.unpack_from('<IIHH', candidate, at)
            assert flags == 0 and patch == packages.paths[pid][0]
            assert old_footer <= offset < offset + size <= new_footer
            assert 0 < size <= 0x40000
            payload = candidate[offset:offset + size]
            source = packages.block(pid, index)
            assert len(payload) == len(source)
            changes = {i: (a, b) for i, (a, b) in enumerate(zip(source, payload)) if a != b}
            assert changes == expected.get(key, {}), (row['package'], index)
            assert candidate[at + 12:at + 32] == hashlib.sha1(payload).digest()
            assert candidate[at + 32:at + 48] == bytes(16)
            restored_prefix[at:at + 48] = original[at:at + 48]
            total_changed += len(changes)
        assert restored_prefix == original[:old_footer]
        # a native tag uses its first block's patch for all reads, unlike the Python reader's per-block choice
        for entry_at in range(0, len(entries), 16):
            value = struct.unpack_from('<Q', entries, entry_at + 8)[0]
            first, skip, size = value & 0x3fff, ((value >> 14) & 0x3fff) * 16, value >> 28
            if not size:
                continue
            span = set(range(first, first + (skip + size - 1) // 0x40000 + 1))
            if span & required[pid]:
                assert span <= required[pid]
                patches = {struct.unpack_from('<H', candidate, table_at + index * 48 + 8)[0]
                           for index in span}
                assert patches == {packages.paths[pid][0]}
        total_entries += len(entries) // 16
    assert checked == {(pid, block) for pid, blocks in required.items() for block in blocks}
    result = {'packages': len(receipt['packages']), 'tags_changed': len(receipt['tags']),
              'entry_records_preserved': total_entries, 'decoded_bytes_changed': total_changed,
              'changed_blocks': len(expected), 'rewritten_blocks': len(checked),
              'relocation_only_blocks': len(checked) - len(expected),
              'native_patch_span_closure_verified': True,
              'native_header_hash_spans_verified': True,
              'all_other_decoded_bytes_preserved': True,
              'native_body_loader_validated': False}
    (staged.parent / 'independent-content-verification.json').write_text(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--staged', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(Packages(args.game_root, args.pid), args.staged)))
