"""Refresh v38 metadata-span SHA1 fields after package table edits. Cached span: header+F0 offset, +F4 length, +F8 SHA1; extended: +110, +114, +118. Does not fix the RSA footer."""
import hashlib
import struct


def refresh_header_hashes(package):
    if len(package) < 0x170 or struct.unpack_from('<H', package)[0] != 38:
        raise ValueError('Unsupported native package header version')
    footer, size = struct.unpack_from('<II', package, 0x160)
    if size != len(package) or footer < 0x170 or footer + 0x800 != size:
        raise ValueError('Unsupported native package footer layout')
    rows = []
    for name, field in (('cached', 0xf0), ('extended', 0x110)):
        start, count = struct.unpack_from('<II', package, field)
        old = bytes(package[field + 8:field + 28])
        if count:
            if start > footer or count > footer - start or start < 0x12c:
                raise ValueError('Native metadata hash span is outside package data')
            digest = hashlib.sha1(package[start:start + count]).digest()
        else:
            # native count 0 skips this check and some originals store zero rather than SHA1(empty); keep that exact
            digest = old
        rows.append({'segment': name, 'field': field, 'offset': start,
                     'bytes': count, 'previous_sha1': old.hex(), 'sha1': digest.hex()})
    for row in rows:
        if row['bytes']:
            at = row['field'] + 8
            package[at:at + 20] = bytes.fromhex(row['sha1'])
    return rows
