import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from stage_native_content import apply_edits, native_span_closure, stage
from package_header_hashes import refresh_header_hashes
from verify_staged_content import verify


def entry(first, offset, size, cls=0x80800001):
    return struct.pack('<IIQ', cls, 0, first | ((offset // 16) << 14) | (size << 28))


class SpanFixture:
    def __init__(self, root):
        self.root = root
        (root / 'packages').mkdir()
        self.path = root / 'packages/w64_fixture_0001_2.pkg'
        self.payloads = tuple(bytes([value]) * 0x40000 for value in range(4))
        self.entries = (entry(1, 16, 32) + entry(2, 0x3fff0, 32)
                        + entry(1, 0x3fff0, 32) + entry(0, 0x3fff0, 32))
        raw = bytearray(0x1000 + 4 * 0x40000 + 0x800)
        struct.pack_into('<H', raw, 0, 38)
        struct.pack_into('<H', raw, 4, 1)
        struct.pack_into('<H', raw, 0x20, 2)
        struct.pack_into('<I', raw, 0xb4, 4)
        struct.pack_into('<I', raw, 0xd0, 4)
        struct.pack_into('<II', raw, 0x110, 0x800, 0x180)
        struct.pack_into('<II', raw, 0xf0, 0x400, 32)
        raw[0x400:0x420] = b'C' * 32
        struct.pack_into('<II', raw, 0x160, len(raw) - 0x800, len(raw))
        raw[0x860:0x8a0] = self.entries
        records = []
        for index, payload in enumerate(self.payloads):
            at = 0x1000 + index * 0x40000
            records.append(struct.pack('<IIHH', at, len(payload), 0, 0)
                           + hashlib.sha1(payload).digest() + bytes(16))
            raw[at:at + len(payload)] = payload
        self.blocks = b''.join(records)
        raw[0x8c0:0x8c0 + len(self.blocks)] = self.blocks
        raw[-0x800:] = b'F' * 0x800
        refresh_header_hashes(raw)
        self.original = bytes(raw)
        self.path.write_bytes(raw)
        self.paths = {1: (2, self.path)}

    def table(self, pid):
        assert pid == 1
        return self.path, self.entries, self.blocks

    def block(self, pid, block):
        assert pid == 1
        return self.payloads[block]

    def read(self, tag):
        index = tag & 0x1fff
        value = struct.unpack_from('<Q', self.entries, index * 16 + 8)[0]
        first, skip, size = value & 0x3fff, ((value >> 14) & 0x3fff) * 16, value >> 28
        return 0x80800001, b''.join(self.payloads[first:])[skip:skip + size]

    def manifest(self):
        body = self.read(0x80802000)[1]
        return {'tags': [{'tag': '0x80802000', 'class': '0x80800001',
                         'sha256': hashlib.sha256(body).hexdigest(),
                         'edits': [{'offset': 0, 'before': '01', 'after': 'ff'}]}]}


class Fixture:
    def __init__(self, root):
        self.root = root
        packages = root / 'packages'
        packages.mkdir()
        self.path = packages / 'w64_fixture_0001_0.pkg'
        self.payloads = (bytes(range(256)) * 16, bytes(range(255, -1, -1)) * 16)
        self.entries = struct.pack('<IIQIIQ', 0x80800001, 0, (4096 << 28),
                                   0x80800002, 0, (4096 << 28) | 1)
        raw = bytearray(0x4000)
        struct.pack_into('<H', raw, 0, 38)
        struct.pack_into('<H', raw, 4, 1)
        struct.pack_into('<I', raw, 0xb4, 2)
        struct.pack_into('<I', raw, 0xd0, 2)
        struct.pack_into('<II', raw, 0x160, 0x3800, 0x4000)
        struct.pack_into('<II', raw, 0x110, 0x800, 0x100)
        struct.pack_into('<II', raw, 0xf0, 0x400, 32)
        raw[0x400:0x420] = b'C' * 32
        raw[0x860:0x880] = self.entries
        rows = []
        for index, payload in enumerate(self.payloads):
            offset = 0x1000 + index * 0x1000
            rows.append(struct.pack('<IIHH', offset, len(payload), 0, 0)
                        + hashlib.sha1(payload).digest() + bytes(16))
            raw[offset:offset + len(payload)] = payload
        self.blocks = b''.join(rows)
        raw[0x8a0:0x8a0 + len(self.blocks)] = self.blocks
        raw[0x3800:] = b'F' * 0x800
        refresh_header_hashes(raw)
        self.original = bytes(raw)
        self.path.write_bytes(raw)
        self.paths = {1: (0, self.path)}

    def table(self, pid):
        assert pid == 1
        return self.path, self.entries, self.blocks

    def read(self, tag):
        index = tag & 0x1fff
        return 0x80800001 + index, self.payloads[index]

    def block(self, pid, block):
        assert pid == 1
        return self.payloads[block]

    def manifest(self):
        return {'tags': [{'tag': '0x80802000', 'class': '0x80800001',
                         'sha256': hashlib.sha256(self.payloads[0]).hexdigest(),
                         'edits': [{'offset': 4, 'before': '0405', 'after': 'fffe'}]}]}


class StagingTests(unittest.TestCase):
    def test_writer_refreshes_native_extended_digest_without_changing_cached_digest(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            out = fixture.root / 'stage'
            stage(fixture, fixture.manifest(), out)
            body = (out / fixture.path.name).read_bytes()
            start, size = struct.unpack_from('<II', body, 0x110)
            self.assertEqual(body[0x118:0x12c], hashlib.sha1(body[start:start + size]).digest())
            self.assertNotEqual(body[0x118:0x12c], fixture.original[0x118:0x12c])
            self.assertEqual(body[0xf0:0x10c], fixture.original[0xf0:0x10c])
            self.assertEqual(body[-0x800:], fixture.original[-0x800:])
            self.assertTrue(verify(fixture, out)['native_header_hash_spans_verified'])

    def test_independent_verifier_rejects_previous_stale_extended_digest(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            out = fixture.root / 'stage'
            receipt = stage(fixture, fixture.manifest(), out)
            path = out / fixture.path.name
            body = bytearray(path.read_bytes())
            body[0x118:0x12c] = fixture.original[0x118:0x12c]
            path.write_bytes(body)
            receipt['packages'][0]['staged_sha256'] = hashlib.sha256(body).hexdigest()
            (out / 'staging-receipt.json').write_text(json.dumps(receipt))
            with self.assertRaises(AssertionError):
                verify(fixture, out)

    def test_malformed_hash_span_does_not_partially_change_digest_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            body = bytearray(fixture.original)
            body[0x400] ^= 1
            struct.pack_into('<II', body, 0x110, 0x37f8, 32)
            before = bytes(body)
            with self.assertRaisesRegex(ValueError, 'hash span'):
                refresh_header_hashes(body)
            self.assertEqual(body, before)
            struct.pack_into('<II', body, 0x110, 0x118, 32)
            before = bytes(body)
            with self.assertRaisesRegex(ValueError, 'hash span'):
                refresh_header_hashes(body)
            self.assertEqual(body, before)

    def test_absent_hash_segment_preserves_original_zero_representation(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            body = bytearray(fixture.original)
            body[0xf0:0x10c] = bytes(28)
            refresh_header_hashes(body)
            self.assertEqual(body[0xf0:0x10c], bytes(28))

    def test_writer_rejects_block_table_outside_extended_hash_span(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            body = bytearray(fixture.original)
            struct.pack_into('<I', body, 0x114, 0x80)
            fixture.path.write_bytes(body)
            with self.assertRaisesRegex(ValueError, 'extended hash span'):
                stage(fixture, fixture.manifest(), fixture.root / 'stage')
            self.assertFalse((fixture.root / 'stage' / fixture.path.name).exists())

    def test_transitive_span_closure_preserves_native_single_patch_reads(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = SpanFixture(Path(temp))
            out = fixture.root / 'stage'
            receipt = stage(fixture, fixture.manifest(), out)
            rows = receipt['packages'][0]['blocks']
            self.assertEqual([row['index'] for row in rows], [0, 1, 2, 3])
            self.assertEqual([row['index'] for row in rows if row['logical_bytes_changed']], [1])
            candidate = (out / fixture.path.name).read_bytes()
            table = 0x8c0
            for index in range(4):
                value = struct.unpack_from('<Q', fixture.entries, index * 16 + 8)[0]
                first, skip, size = value & 0x3fff, ((value >> 14) & 0x3fff) * 16, value >> 28
                selected = struct.unpack_from('<H', candidate, table + first * 48 + 8)[0]
                self.assertEqual(selected, 2)
                body = bytearray()
                block = first
                while len(body) < size:
                    offset, count, patch, flags = struct.unpack_from('<IIHH', candidate, table + block * 48)
                    self.assertEqual(patch, selected)
                    self.assertEqual(flags, 0)
                    body.extend(candidate[offset + skip:offset + min(count, skip + size - len(body))])
                    block += 1
                    skip = 0
                expected = fixture.read(0x80802000 + index)[1]
                if index == 0:
                    expected = b'\xff' + expected[1:]
                self.assertEqual(bytes(body), expected)
            verified = verify(fixture, out)
            self.assertEqual(verified['changed_blocks'], 1)
            self.assertEqual(verified['relocation_only_blocks'], 3)
            self.assertEqual(verified['decoded_bytes_changed'], 1)
            self.assertTrue(verified['native_patch_span_closure_verified'])
            self.assertEqual(fixture.path.read_bytes(), fixture.original)

    def test_verifier_rejects_missing_continuation_relocation(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = SpanFixture(Path(temp))
            out = fixture.root / 'stage'
            receipt = stage(fixture, fixture.manifest(), out)
            path = out / fixture.path.name
            candidate = bytearray(path.read_bytes())
            candidate[0x8c0:0x8c0 + 48] = fixture.blocks[:48]
            refresh_header_hashes(candidate)
            path.write_bytes(candidate)
            receipt['packages'][0]['staged_sha256'] = hashlib.sha256(candidate).hexdigest()
            receipt['packages'][0]['blocks'] = receipt['packages'][0]['blocks'][1:]
            (out / 'staging-receipt.json').write_text(json.dumps(receipt))
            with self.assertRaises(AssertionError):
                verify(fixture, out)

    def test_span_closure_checks_bounds_and_exact_boundary(self):
        self.assertEqual(native_span_closure(entry(0, 0, 0x40000), 2, {0}), {0})
        self.assertEqual(native_span_closure(entry(0, 0x3fff0, 16), 2, {0}), {0})
        with self.assertRaisesRegex(ValueError, 'span exceeds'):
            native_span_closure(entry(1, 0x3fff0, 32), 2, {1})

    def test_exact_copy_with_one_changed_block(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture = Fixture(root)
            out = root / 'stage'
            receipt = stage(fixture, fixture.manifest(), out)
            data = (out / fixture.path.name).read_bytes()
            self.assertEqual(fixture.path.read_bytes(), fixture.original)
            offset, size, patch, flags = struct.unpack_from('<IIHH', data, 0x8a0)
            expected = fixture.payloads[0][:4] + b'\xff\xfe' + fixture.payloads[0][6:]
            self.assertEqual(data[offset:offset + size], expected)
            self.assertEqual(data[-0x800:], fixture.original[-0x800:])
            self.assertEqual((patch, flags), (0, 0))
            self.assertEqual(struct.unpack_from('<I', data, 0x164)[0], len(data))
            allowed = set(range(0x160, 0x168)) | set(range(0x118, 0x12c)) | set(range(0x8a0, 0x8d0))
            differences = {i for i in range(0x3800) if data[i] != fixture.original[i]}
            self.assertLessEqual(differences, allowed)
            self.assertFalse(receipt['native_load_validated'])
            self.assertEqual(json.loads((out / 'staging-receipt.json').read_text()), receipt)

    def test_reject_installed_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            with self.assertRaisesRegex(ValueError, 'installed packages'):
                stage(fixture, fixture.manifest(), fixture.root / 'packages')

    def test_reject_preimage_before_any_write(self):
        with tempfile.TemporaryDirectory() as temp:
            fixture = Fixture(Path(temp))
            manifest = fixture.manifest()
            manifest['tags'][0]['edits'][0]['before'] = '0000'
            out = fixture.root / 'stage'
            with self.assertRaisesRegex(ValueError, 'Preimage mismatch'):
                stage(fixture, manifest, out)
            self.assertFalse(out.exists())

    def test_reject_size_bounds_and_overlap(self):
        for edits in ([{'offset': 0, 'before': '00', 'after': '0102'}],
                      [{'offset': -1, 'before': '00', 'after': '01'}],
                      [{'offset': 4, 'before': '00', 'after': '01'}],
                      [{'offset': 0, 'before': '00', 'after': '01'}] * 2):
            with self.subTest(edits=edits), self.assertRaises(ValueError):
                apply_edits(bytes(4), edits)


if __name__ == '__main__':
    unittest.main()
